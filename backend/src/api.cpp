#include "api.hpp"

#include "webserver.hpp"
#include "world.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/string.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <utility>

namespace json = boost::json;

namespace {
std::string String(const json::object& object, const char* key) {
    const auto* value = object.if_contains(key);
    if (!value || !value->is_string()) throw ApiError(400, "INVALID_REQUEST", std::string("Ожидается строка: ") + key);
    return std::string(value->as_string());
}

std::int64_t Integer(const json::object& object, const char* key) {
    const auto* value = object.if_contains(key);
    if (!value || !value->is_int64()) throw ApiError(400, "INVALID_REQUEST", std::string("Ожидается целое число: ") + key);
    return value->as_int64();
}

float Coordinate(const json::object& object, const char* key) {
    const auto* value = object.if_contains(key);
    if (!value || !value->is_number())
        throw ApiError(400, "INVALID_REQUEST", std::string("Ожидается координата: ") + key);
    const double number = value->to_number<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() / 4.0)
        throw ApiError(400, "INVALID_REQUEST", "Координаты должны быть конечными числами.");
    return static_cast<float>(number);
}

ApiReply Success(json::value result) {
    return {200, json::object{{"ok", true}, {"result", std::move(result)}}, {}, false};
}
}

ApiReply ApiFailure(int status, const std::string& code, const std::string& message) {
    return {status, json::object{{"ok", false}, {"error", json::object{
        {"status", status}, {"code", code}, {"message", message}}}}, {}, false};
}

void ApiService::LimitAttempt(const std::string& identity) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(attempts_mutex_);
    for (auto it = attempts_.begin(); it != attempts_.end();) {
        if (now - it->second.start >= std::chrono::minutes(1)) it = attempts_.erase(it);
        else ++it;
    }
    auto it = attempts_.find(identity);
    if (it == attempts_.end()) {
        if (attempts_.size() >= 4096) throw ApiError(429, "RATE_LIMITED", "Повторите попытку через минуту.");
        it = attempts_.emplace(identity, Attempts{now, 0}).first;
    }
    if (++it->second.count > 10) throw ApiError(429, "RATE_LIMITED", "Слишком много попыток. Повторите через минуту.");
}

ApiReply ApiService::Handle(const std::string& body, const std::string& credential,
                            const std::string& client_ip, bool http) {
    try {
        boost::system::error_code error;
        const auto value = json::parse(body, error);
        if (error || !value.is_object()) throw ApiError(400, "INVALID_JSON", "Ожидается JSON-объект.");
        const auto& request = value.as_object();
        if (String(request, "type") != "request") throw ApiError(400, "INVALID_REQUEST", "Неизвестный тип сообщения.");
        const auto command = String(request, "request_type");
        if (command == "register" || command == "login") {
            if (!http) throw ApiError(400, "HTTP_REQUIRED", "Используйте POST /api для входа и регистрации.");
            const auto username = String(request, "username");
            if (username.size() > 32) throw ApiError(400, "INVALID_USERNAME", "Логин слишком длинный.");
            LimitAttempt("ip:" + client_ip);
            std::string canonical = username;
            for (char& c : canonical) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            LimitAttempt("user:" + canonical);
            const auto password = String(request, "password");
            const auto token = command == "register" ? auth_.Register(username, password) : auth_.Login(username, password);
            if (const auto old = auth_.Authenticate(credential); old && old->is_session) auth_.Logout(*old);
            auto reply = Success(auth_.Profile(*auth_.Authenticate(token)));
            reply.session_token = token;
            return reply;
        }

        const auto context = auth_.Authenticate(credential);
        if (!context) throw ApiError(401, "AUTH_REQUIRED", "Войдите в аккаунт.");
        const Principal* principal = context->principal;
        if (command == "me") return Success(auth_.Profile(*context));
        if (command == "logout") {
            if (!http) throw ApiError(400, "HTTP_REQUIRED", "Используйте POST /api для выхода.");
            auth_.Logout(*context);
            auto reply = Success(json::object{{"logged_out", true}});
            reply.clear_session = true;
            return reply;
        }
        if (command == "create_api_key") {
            if (!context->is_session) throw ApiError(403, "SESSION_REQUIRED", "Для создания ключей войдите с паролем.");
            LimitAttempt("ip:" + client_ip);
            LimitAttempt("key:" + std::to_string(principal->user_id));
            const auto days = Integer(request, "expires_days");
            if (days < 1 || days > 365) throw ApiError(400, "INVALID_KEY", "Срок ключа: от 1 до 365 дней.");
            return Success(auth_.CreateKey(*context, String(request, "name"), static_cast<int>(days), String(request, "password")));
        }
        if (command == "list_api_keys") return Success(auth_.ListKeys(*context));
        if (command == "revoke_api_key") {
            auth_.RevokeKey(*context, Integer(request, "key_id"));
            return Success(json::object{{"revoked", true}});
        }


/*
principal->user_id

Вот тут
*/

        if (command == "create_craft") {
            if (!context->is_session)
                throw ApiError(403, "INSUFFICIENT_SCOPE", "API-ключ не разрешает создавать корабли.");
            return Success(CreateEntity(&world_, principal));
        }

        std::lock_guard<std::mutex> lock(world_.GetMutex());
        if (command == "fly_to") {
            const auto id = Integer(request, "entity_id");
            const Vector2 target{Coordinate(request, "x"), Coordinate(request, "y")};
            const auto entities = world_.GetEntities();
            const auto it = id >= 0 ? entities.find(static_cast<std::size_t>(id)) : entities.end();
            if (it == entities.end()) throw ApiError(404, "NOT_FOUND", "Объект не найден.");
            auto* entity = it->second;
            if (entity->owner != principal)
                throw ApiError(403, "FORBIDDEN", "Можно управлять только своими кораблями.");
            if (entity->GetType() != CRAFT)
                throw ApiError(400, "NOT_A_CRAFT", "Объект не является кораблём.");
            bool propulsion = false;
            for (const auto& module : static_cast<Craft*>(entity)->GetModules()) {
                if (module->GetType() == PROPULSION) {
                    static_cast<PropulsionModule*>(module.get())->FlyTo(target);
                    propulsion = true;
                }
            }
            if (!propulsion) throw ApiError(400, "NO_PROPULSION", "У корабля нет двигателя.");
            return Success(GetEntity(entity));
        }
        if (command == "get_map") return Success(GetMap(&world_));
        if (command == "get_my_entities") return Success(GetMyEntities(&world_, principal));
        if (command == "get_entity") {
            const auto id = Integer(request, "entity_id");
            const auto entities = world_.GetEntities();
            const auto it = id >= 0 ? entities.find(static_cast<std::size_t>(id)) : entities.end();
            if (it == entities.end()) throw ApiError(404, "NOT_FOUND", "Объект не найден.");
            return Success(GetEntity(it->second));
        }
        throw ApiError(400, "UNKNOWN_COMMAND", "Неизвестная команда.");
    } catch (const ApiError& error) {
        return ApiFailure(error.status, error.code, error.what());
    } catch (const std::exception&) {
        std::cerr << "API operation failed\n";
        return ApiFailure(500, "INTERNAL_ERROR", "Ошибка сервера.");
    }
}
