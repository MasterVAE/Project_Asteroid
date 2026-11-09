#include "webserver.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
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
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>

#include "world.hpp"


namespace json = boost::json;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;

using tcp = net::ip::tcp;
using error_code = boost::system::error_code;

class Client;

// Контейнер подключённых клиентов
class Server {
public:
    std::unordered_map<std::uint64_t, std::shared_ptr<Client>> clients;
    std::uint64_t next_id = 1;
    World* world = nullptr;
    std::filesystem::path web_root;

    void broadcast(const std::string& message);
    void remove_client(std::uint64_t id);
};

// Одно WebSocket-подключение
class Client : public std::enable_shared_from_this<Client> {
private:
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::deque<std::string> messages_;
    Server& server_;

    std::uint64_t id_ = 0;

public:
    Client(tcp::socket socket, Server& server)
        : ws_(std::move(socket)), server_(server) {}

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
        json::value value = json::parse(message);

        json::object& obj = value.as_object();

        std::string type = obj.at("type").as_string().c_str();

        if(type == "request")
        {
            std::lock_guard<std::mutex> lock(server_.world->GetMutex());
            std::string request_type = obj.at("request_type").as_string().c_str();
            if(request_type == "get_map")
            {
                SendJSON(GetMap(server_.world));
            }
            else if(request_type == "get_entity")
            {
                std::size_t entity_id = obj.at("entity_id").as_int64();
                auto entities = server_.world->GetEntities();
                auto it = entities.find(entity_id);
                if(it != entities.end())
                {
                    SendJSON(GetEntity(it->second));
                }
            }
        }


        //int64_t ship_id = obj.at("ship_id").as_int64();
        //double speed = obj.at("speed").as_double();
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
        parser_.body_limit(0);
        parser_.header_limit(8192);
    }

    void start() {
        stream_.expires_after(std::chrono::seconds(30));
        http::async_read(stream_, buffer_, parser_,
            [self = shared_from_this()](error_code ec, std::size_t) {
                if (!ec) self->respond();
            });
    }

private:
    template<class Body>
    void send(http::response<Body> response) {
        response.keep_alive(false);
        response.set(http::field::server, "Asteroid");
        response.set(http::field::cache_control, "no-cache");
        response.set("X-Content-Type-Options", "nosniff");
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

    void respond() {
        const auto& request = parser_.get();
        std::string target(request.target());
        target = target.substr(0, target.find('?'));
        if (websocket::is_upgrade(request)) {
            // Корневой путь сохраняет совместимость с Python-клиентом.
            if (target != "/" && target != "/ws") {
                error(http::status::not_found, "Unknown WebSocket endpoint\n");
                return;
            }
            std::make_shared<Client>(stream_.release_socket(), server_)->start(request);
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

void LaunchWebServer(World* world, const std::string& web_root)
{
    try {
        net::io_context io;
        Server server;
        server.world = world;
        server.web_root = std::filesystem::absolute(web_root);
        if (!std::filesystem::is_regular_file(server.web_root / "index.html")) {
            std::cerr << "Browser client not found in " << server.web_root
                      << ". Pass its directory as the second argument.\n";
        }

        tcp::acceptor acceptor(
            io, tcp::endpoint(tcp::v4(), 8080)
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

        std::cout << "HTTP + WebSocket server on port 8080\n"
                  << "Open http://SERVER_IP:8080 in your browser\n";

        io.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return;
    }
}
