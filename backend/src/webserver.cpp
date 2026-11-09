#include "webserver.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/stream_base.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/string.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "api.hpp"
#include "auth.hpp"
#include "world.hpp"


namespace json = boost::json;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;

using tcp = net::ip::tcp;
using error_code = boost::system::error_code;

class Client;

struct AuthConfig {
    std::string origin;
    std::string host;
    std::string cookie_name;
    bool secure = true;

    AuthConfig() {
        const char* configured = std::getenv("ASTEROID_PUBLIC_ORIGIN");
        if (!configured) throw std::runtime_error("Set ASTEROID_PUBLIC_ORIGIN, e.g. https://46.173.218.137");
        origin = configured;
        const char* dev = std::getenv("ASTEROID_DEV_AUTH");
        const bool development = dev && std::string(dev) == "1";
        secure = origin.compare(0, 8, "https://") == 0;
        if (!secure && (!development || origin.compare(0, 7, "http://") != 0))
            throw std::runtime_error("Authentication requires HTTPS; local development requires ASTEROID_DEV_AUTH=1");
        host = origin.substr(secure ? 8 : 7);
        if (host.empty() || host.find_first_of("/@?#\\\r\n \t") != std::string::npos)
            throw std::runtime_error("ASTEROID_PUBLIC_ORIGIN must be an origin without a path or trailing slash");
        if (!secure && host != "localhost:8080" && host != "127.0.0.1:8080")
            throw std::runtime_error("Unencrypted development authentication is only allowed on localhost:8080");
        cookie_name = secure ? "__Host-asteroid_session" : "asteroid_dev_session";
    }
};

// Контейнер подключённых клиентов
class Server {
public:
    std::unordered_map<std::uint64_t, std::shared_ptr<Client>> clients;
    std::uint64_t next_id = 1;
    World* world = nullptr;
    std::filesystem::path web_root;
    AuthConfig config;
    AuthStore auth;
    ApiService api;
    net::thread_pool workers{2};
    unsigned pending_requests = 0;

    explicit Server(World& world) : world(&world),
        auth(std::getenv("ASTEROID_AUTH_DB") ? std::getenv("ASTEROID_AUTH_DB") : "data/auth.sqlite3"),
        api(auth, world) {}

    void broadcast(const std::string& message);
    void remove_client(std::uint64_t id);
    void prune_clients();
};

// Одно WebSocket-подключение
class Client : public std::enable_shared_from_this<Client> {
private:
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::deque<std::string> messages_;
    Server& server_;
    std::string credential_;

    std::uint64_t id_ = 0;

public:
    Client(tcp::socket socket, Server& server, std::string credential)
        : ws_(std::move(socket)), server_(server), credential_(std::move(credential)) {}

    bool authorized() { return server_.auth.Authenticate(credential_).has_value(); }

    void stop() {
        error_code ignored;
        ws_.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
        ws_.next_layer().close(ignored);
        server_.remove_client(id_);
    }

    void start(const http::request<http::string_body>& request) {
        ws_.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server
            )
        );

        ws_.read_message_max(64 * 1024);

        ws_.async_accept(
            request,
            [self = shared_from_this()](error_code ec) {
                if (ec) return;

                self->id_ = self->server_.next_id++;
                self->server_.clients[self->id_] = self;

                std::cout << "Connected: " << self->id_ << '\n';

                self->send("Your ID: " + std::to_string(self->id_));
                self->read();
            }
        );
    }

    std::uint64_t id() const {
        return id_;
    }

    // Асинхронное получение сообщений
    void read() {
        ws_.async_read(
            buffer_,
            [self = shared_from_this()](
                error_code ec, std::size_t
            ) {
                if (ec) {
                    self->server_.remove_client(self->id_);
                    return;
                }

                std::string message =
                    beast::buffers_to_string(self->buffer_.data());

                self->buffer_.consume(self->buffer_.size());

                self->ParseJSON(message);

                self->read();
            }
        );
    }

    void ParseJSON(const std::string& message)
    {
        // Expensive account operations are HTTP-only and use the worker pool.
        error_code error;
        const auto value = json::parse(message, error);
        if (!error && value.is_object()) {
            const auto* command = value.as_object().if_contains("request_type");
            if (command && command->is_string() &&
                (command->as_string() == "register" || command->as_string() == "login" ||
                 command->as_string() == "logout" || command->as_string() == "create_api_key" ||
                 command->as_string() == "list_api_keys" || command->as_string() == "revoke_api_key")) {
                SendJSON(ApiFailure(400, "HTTP_REQUIRED", "Используйте POST /api для операций аккаунта.").body);
                return;
            }
        }
        const auto reply = server_.api.Handle(message, credential_, "websocket", false);
        SendJSON(reply.body);
    }

    void SendJSON(const json::value& value) {
        std::string message = json::serialize(value);
        send(message);
    }

    // Добавляем сообщение в очередь отправки
    void send(std::string message) {
        messages_.push_back(std::move(message));

        if (messages_.size() == 1) {
            write();
        }
    }

private:
    // Асинхронная отправка
    void write() {
        ws_.text(true);

        ws_.async_write(
            net::buffer(messages_.front()),
            [self = shared_from_this()](
                error_code ec, std::size_t
            ) {
                if (ec) {
                    self->server_.remove_client(self->id_);
                    return;
                }

                self->messages_.pop_front();

                if (!self->messages_.empty()) {
                    self->write();
                }
            }
        );
    }
};

// HTTP и WebSocket используют один порт. Разрешены только файлы клиента.
class HttpSession : public std::enable_shared_from_this<HttpSession> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request_parser<http::string_body> parser_;
    Server& server_;

public:
    HttpSession(tcp::socket socket, Server& server)
        : stream_(std::move(socket)), server_(server) {
        parser_.body_limit(16 * 1024);
        parser_.header_limit(8192);
    }

    void start() {
        stream_.expires_after(std::chrono::seconds(30));
        http::async_read(stream_, buffer_, parser_,
            [self = shared_from_this()](error_code ec, std::size_t) {
                if (!ec) self->respond();
                else self->send_api(ApiFailure(ec == http::error::body_limit ? 413 : 400,
                    "INVALID_REQUEST", "Некорректный или слишком большой запрос."));
            });
    }

private:
    template<class Body>
    void send(http::response<Body> response) {
        response.keep_alive(false);
        response.set(http::field::server, "Asteroid");
        if (response[http::field::cache_control].empty()) response.set(http::field::cache_control, "no-cache");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Referrer-Policy", "same-origin");
        response.set("Content-Security-Policy", "default-src 'self'; connect-src 'self' " +
            std::string(server_.config.secure ? "wss://" : "ws://") + server_.config.host +
            "; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
        auto message = std::make_shared<http::response<Body>>(std::move(response));
        http::async_write(stream_, *message,
            [self = shared_from_this(), message](error_code, std::size_t) {
                error_code ignored;
                self->stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
            });
    }

    void error(http::status status, const std::string& message) {
        http::response<http::string_body> response{status, parser_.get().version()};
        response.set(http::field::content_type, "text/plain; charset=utf-8");
        response.content_length(message.size());
        if (parser_.get().method() != http::verb::head) response.body() = message;
        if (status == http::status::method_not_allowed) response.set(http::field::allow, "GET, HEAD");
        send(std::move(response));
    }

    void send_api(const ApiReply& reply) {
        http::response<http::string_body> response{static_cast<http::status>(reply.status), 11};
        response.set(http::field::content_type, "application/json; charset=utf-8");
        response.set(http::field::cache_control, "no-store");
        if (!reply.session_token.empty() || reply.clear_session) {
            response.set(http::field::set_cookie, server_.config.cookie_name + "=" + reply.session_token +
                "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + (reply.clear_session ? "0" : "86400") +
                (server_.config.secure ? "; Secure" : ""));
        }
        if (reply.status == 429) response.set(http::field::retry_after, "60");
        response.body() = json::serialize(reply.body);
        response.prepare_payload();
        send(std::move(response));
    }

    std::string credential() {
        const auto& request = parser_.get();
        const std::string authorization(request[http::field::authorization]);
        if (!authorization.empty()) {
            if (authorization.compare(0, 7, "Bearer ") != 0)
                throw ApiError(401, "AUTH_REQUIRED", "Ожидается Authorization: Bearer <ключ>.");
            return authorization.substr(7);
        }
        const std::string cookies(request[http::field::cookie]);
        std::size_t position = 0;
        const auto prefix = server_.config.cookie_name + "=";
        while (position < cookies.size()) {
            while (position < cookies.size() && (cookies[position] == ' ' || cookies[position] == ';')) ++position;
            const auto end = cookies.find(';', position);
            if (cookies.compare(position, prefix.size(), prefix) == 0)
                return cookies.substr(position + prefix.size(), end == std::string::npos ? end : end - position - prefix.size());
            if (end == std::string::npos) break;
            position = end + 1;
        }
        return {};
    }

    void validate_origin() {
        const auto& request = parser_.get();
        if (request[http::field::host] != server_.config.host)
            throw ApiError(403, "INVALID_HOST", "Откройте сайт по его публичному адресу.");
        const auto origin = request[http::field::origin];
        if (!origin.empty() && origin != server_.config.origin)
            throw ApiError(403, "INVALID_ORIGIN", "Запрос с другого сайта запрещён.");
        if (server_.config.secure && request["X-Forwarded-Proto"] != "https")
            throw ApiError(403, "HTTPS_REQUIRED", "Подключитесь через HTTPS/WSS.");
    }

    void handle_api() {
        try {
            validate_origin();
            const auto& request = parser_.get();
            if (request.method() != http::verb::post)
                throw ApiError(405, "METHOD_NOT_ALLOWED", "Используйте POST /api.");
            // Custom header + JSON require preflight for cross-origin browser requests.
            // No CORS permissions are granted, including for requests without Origin.
            if (request["X-Asteroid-Client"] != "1")
                throw ApiError(403, "CSRF_REJECTED", "Отсутствует заголовок X-Asteroid-Client.");
            const std::string content_type(request[http::field::content_type]);
            if (content_type != "application/json" && content_type.compare(0, 17, "application/json;") != 0)
                throw ApiError(415, "INVALID_CONTENT_TYPE", "Ожидается application/json.");
            if (server_.pending_requests >= 16)
                throw ApiError(429, "SERVER_BUSY", "Сервер занят. Повторите запрос позже.");
            const auto token = credential();
            error_code error;
            auto address = stream_.socket().remote_endpoint(error).address();
            if (!request["X-Real-IP"].empty()) {
                address = net::ip::make_address(std::string(request["X-Real-IP"]), error);
                if (error) throw ApiError(400, "INVALID_REQUEST", "Некорректный адрес клиента.");
            }
            const auto client_ip = address.to_string();
            const auto body = request.body();
            const auto executor = stream_.get_executor();
            ++server_.pending_requests;
            net::post(server_.workers, [self = shared_from_this(), body, token, client_ip, executor] {
                auto reply = self->server_.api.Handle(body, token, client_ip, true);
                net::post(executor, [self, reply = std::move(reply)] {
                    --self->server_.pending_requests;
                    self->send_api(reply);
                    self->server_.prune_clients();
                });
            });
        } catch (const ApiError& error) {
            send_api(ApiFailure(error.status, error.code, error.what()));
        }
    }

    void respond() {
        const auto& request = parser_.get();
        std::string target(request.target());
        target = target.substr(0, target.find('?'));
        if (target == "/api") {
            handle_api();
            return;
        }
        if (websocket::is_upgrade(request)) {
            // Корневой путь сохраняет совместимость с Python-клиентом.
            if (target != "/" && target != "/ws") {
                error(http::status::not_found, "Unknown WebSocket endpoint\n");
                return;
            }
            try {
                validate_origin();
                const auto token = credential();
                if (!server_.auth.Authenticate(token)) throw ApiError(401, "AUTH_REQUIRED", "Войдите в аккаунт.");
                std::make_shared<Client>(stream_.release_socket(), server_, token)->start(request);
            } catch (const ApiError& error) {
                send_api(ApiFailure(error.status, error.code, error.what()));
            }
            return;
        }
        if (request.method() != http::verb::get && request.method() != http::verb::head) {
            error(http::status::method_not_allowed, "Use GET or HEAD\n");
            return;
        }
        static const std::unordered_map<std::string, std::pair<std::string, std::string>> files{
            {"/", {"index.html", "text/html; charset=utf-8"}},
            {"/index.html", {"index.html", "text/html; charset=utf-8"}},
            {"/style.css", {"style.css", "text/css; charset=utf-8"}},
            {"/app.js", {"app.js", "text/javascript; charset=utf-8"}},
            {"/interpolation.js", {"interpolation.js", "text/javascript; charset=utf-8"}},
            {"/asteroidAPI.js", {"asteroidAPI.js", "text/javascript; charset=utf-8"}}
        };
        const auto file = files.find(target);
        if (file == files.end()) {
            error(http::status::not_found, "Not found\n");
            return;
        }
        http::file_body::value_type body;
        error_code ec;
        body.open((server_.web_root / file->second.first).string().c_str(), beast::file_mode::scan, ec);
        if (ec) {
            error(http::status::not_found, "Client file not found. Check the server web_root.\n");
            return;
        }
        if (request.method() == http::verb::head) {
            http::response<http::empty_body> response{http::status::ok, request.version()};
            response.set(http::field::content_type, file->second.second);
            response.content_length(body.size());
            send(std::move(response));
        } else {
            http::response<http::file_body> response{http::status::ok, request.version()};
            response.set(http::field::content_type, file->second.second);
            response.body() = std::move(body);
            response.prepare_payload();
            send(std::move(response));
        }
    }
};

// Отправка сообщения всем клиентам
void Server::broadcast(const std::string& message) {
    for (auto& [id, client] : clients) {
        client->send(message);
    }
}

// Удаление отключённого клиента
void Server::remove_client(std::uint64_t id) {
    if (clients.erase(id)) {
        std::cout << "Disconnected: " << id << '\n';
    }
}

void Server::prune_clients() {
    std::vector<std::shared_ptr<Client>> expired;
    for (const auto& entry : clients) if (!entry.second->authorized()) expired.push_back(entry.second);
    for (const auto& client : expired) client->stop();
}

void LaunchWebServer(World* world, const std::string& web_root)
{
    try {
        net::io_context io;
        Server server(*world);
        server.web_root = std::filesystem::absolute(web_root);
        if (!std::filesystem::is_regular_file(server.web_root / "index.html")) {
            std::cerr << "Browser client not found in " << server.web_root
                      << ". Pass its directory as the second argument.\n";
        }

        tcp::acceptor acceptor(
            io,
            tcp::endpoint(net::ip::address_v4::loopback(), 8080)
        );

        std::function<void()> accept_next;

        accept_next = [&]() {
            acceptor.async_accept(
                [&](error_code ec, tcp::socket socket) {
                    if (!ec) {
                        std::make_shared<HttpSession>(
                            std::move(socket), server
                        )->start();
                    } else {
                        std::cerr << "Accept error: "
                                  << ec.message() << '\n';
                    }

                    accept_next();
                }
            );
        };

        accept_next();

        net::steady_timer session_timer(io);
        std::function<void()> check_sessions;
        check_sessions = [&] {
            session_timer.expires_after(std::chrono::seconds(15));
            session_timer.async_wait([&](error_code error) {
                if (error) return;
                server.prune_clients();
                check_sessions();
            });
        };
        check_sessions();

        std::cout << "HTTP + WebSocket server on port 8080\n"
                  << "Open " << server.config.origin << " in your browser\n";

        io.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return;
    }
}
