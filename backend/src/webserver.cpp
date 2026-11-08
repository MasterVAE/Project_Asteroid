#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

#include "webserver.hpp"
#include "world.hpp"


namespace json = boost::json;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

using tcp = net::ip::tcp;
using error_code = boost::system::error_code;

class Client;

// Контейнер подключённых клиентов
class Server {
public:
    std::unordered_map<uint64_t, std::shared_ptr<Client>> clients;
    uint64_t next_id = 1;
    World* world = nullptr;

    void broadcast(const std::string& message);
    void remove_client(uint64_t id);
};

// Одно WebSocket-подключение
class Client : public std::enable_shared_from_this<Client> {
private:
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::deque<std::string> messages_;
    Server& server_;

    uint64_t id_ = 0;

public:
    Client(tcp::socket socket, Server& server)
        : ws_(std::move(socket)), server_(server) {}

    void start() {
        ws_.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server
            )
        );

        ws_.read_message_max(64 * 1024);

        ws_.async_accept(
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

    uint64_t id() const {
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

                std::cout << "Client " << self->id_
                          << ": " << message << '\n';

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

// Отправка сообщения всем клиентам
void Server::broadcast(const std::string& message) {
    for (auto& [id, client] : clients) {
        client->send(message);
    }
}

// Удаление отключённого клиента
void Server::remove_client(uint64_t id) {
    if (clients.erase(id)) {
        std::cout << "Disconnected: " << id << '\n';
    }
}

void LaunchWebServer(World* world)
{
    try {
        net::io_context io;
        Server server;
        server.world = world;

        tcp::acceptor acceptor(
            io, tcp::endpoint(tcp::v4(), 8080)
        );

        std::function<void()> accept_next;

        accept_next = [&]() {
            acceptor.async_accept(
                [&](error_code ec, tcp::socket socket) {
                    if (!ec) {
                        std::make_shared<Client>(
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

        std::cout << "WebSocket server on port 8080\n";

        io.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return;
    }
}