#include "web_server.hpp"
#include "accounts.hpp"
#include "game_server.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <deque>
#include <functional>
#include <fcntl.h>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace {
std::string setting(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

std::vector<uint8_t> handle_packet(const std::string& bytes, Connection& connection, Accounts& accounts, World& world) {
    protocol::Reader reader(bytes);
    const auto type = static_cast<protocol::Request>(reader.u8());
    switch (type) {
    case protocol::Request::Login:
    case protocol::Request::Register: {
        const auto credentials = reader.credentials();
        reader.finish();
        if (connection.IsLoggedIn()) return protocol::error("Сначала выйдите из аккаунта.");
        auto response = accounts.authenticate(credentials, type == protocol::Request::Register);
        if (response.status == protocol::Status::Ok &&
            !connection.Login(response.user_id, response.username))
            return protocol::error("Этот игрок уже подключён с другого соединения.");
        return protocol::encode(response);
    }
    case protocol::Request::Logout:
        reader.finish();
        connection.Logout();
        return {static_cast<uint8_t>(protocol::Response::Logout)};
    case protocol::Request::AttachScript:
    case protocol::Request::StopScript:
    case protocol::Request::InspectScript: {
        const auto craft_id = reader.u32();
        const auto source = type == protocol::Request::AttachScript ? reader.long_string() : std::string{};
        reader.finish();
        std::shared_ptr<ScriptRuntime> previous;
        protocol::Writer out;
        {
            std::lock_guard<std::mutex> lock(world.mutex);
            if (!connection.player) return protocol::error("Сначала войдите.");
            Craft* target = nullptr;
            for (auto& craft : world.crafts)
                if (craft.GetId() == craft_id) { target = &craft; break; }
            if (!target || target->GetOwner() != connection.player)
                return protocol::error("Управлять скриптом может только владелец судна.");
            if (type == protocol::Request::AttachScript) {
                auto runtime = std::make_shared<ScriptRuntime>(source);
                previous = std::move(target->script);
                if (previous) previous->Stop();
                target->script = std::move(runtime);
                target->script_source = source;
            } else if (type == protocol::Request::StopScript && target->script) target->script->Stop();
            out.u8(static_cast<uint8_t>(protocol::Response::Script));
            out.u32(craft_id);
            out.u8(static_cast<uint8_t>(target->script ? target->script->state() : ScriptRuntime::State::None));
            out.string(target->script ? target->script->log() : "");
        }
        // The old worker may wait for Docker cleanup; never join under world.mutex.
        return out.bytes;
    }
    default:
        return protocol::error("Неизвестный тип запроса.");
    }
}

template<class Stream>
void serve_websocket(Stream stream, const http::request<http::string_body>& request,
                     Accounts& accounts, World& world) {
    websocket::stream<Stream> ws(std::move(stream));
    ws.read_message_max(protocol::MaxMessageBytes);
    ws.accept(request);
    ws.binary(true);
    Connection connection(world);
    beast::flat_buffer buffer;
    net::steady_timer timer(ws.get_executor());
    std::deque<std::shared_ptr<std::vector<uint8_t>>> outgoing;
    size_t queued_bytes = 0;
    bool stopped = false;
    auto stop = [&] {
        if (stopped) return;
        stopped = true;
        timer.cancel();
        beast::error_code ec;
        beast::get_lowest_layer(ws).cancel(ec);
        beast::get_lowest_layer(ws).close(ec);
    };
    std::function<void()> write, read, tick;
    write = [&] {
        ws.async_write(net::buffer(*outgoing.front()), [&](beast::error_code ec, size_t) {
            if (ec) { stop(); return; }
            queued_bytes -= outgoing.front()->size();
            outgoing.pop_front();
            if (!stopped && !outgoing.empty()) write();
        });
    };
    auto enqueue = [&](std::vector<uint8_t> bytes) {
        if (stopped) return;
        if (bytes.size() > protocol::MaxMessageBytes || queued_bytes + bytes.size() > 4 * 1024 * 1024) {
            stop(); return; // Disconnect slow consumers instead of accumulating snapshots.
        }
        const bool idle = outgoing.empty();
        queued_bytes += bytes.size();
        outgoing.push_back(std::make_shared<std::vector<uint8_t>>(std::move(bytes)));
        if (idle) write();
    };
    read = [&] {
        ws.async_read(buffer, [&](beast::error_code ec, size_t) {
            if (ec || !ws.got_binary()) { stop(); return; }
            try {
                enqueue(handle_packet(beast::buffers_to_string(buffer.data()), connection, accounts, world));
            } catch (const std::invalid_argument& error) {
                enqueue(protocol::error(error.what()));
            } catch (const std::exception& error) {
                std::cerr << "Request: " << error.what() << '\n';
                enqueue(protocol::error("Ошибка сервера."));
            }
            buffer.consume(buffer.size());
            if (!stopped) read();
        });
    };
    tick = [&] {
        if (stopped) return;
        enqueue(EncodeWorld(world));
        if (stopped) return;
        timer.expires_after(std::chrono::milliseconds(100));
        timer.async_wait([&](beast::error_code ec) { if (!ec) tick(); });
    };
    read();
    tick();
    // Each connection has its own io_context. All writes run on this thread.
    static_cast<net::io_context&>(ws.get_executor().context()).run();
}

template<class Stream>
void serve_http(Stream stream, Accounts& accounts, World& world, const std::string& frontend) {
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(4096);
    http::read(stream, buffer, parser);
    auto request = parser.release();
    if (request.target() == "/ws" && websocket::is_upgrade(request)) {
        serve_websocket(std::move(stream), request, accounts, world);
        return;
    }
    const auto target = request.target().substr(0, request.target().find('?'));
    std::string file, mime;
    if (target == "/" || target == "/index.html") { file = "index.html"; mime = "text/html; charset=utf-8"; }
    if (target == "/style.css") { file = "style.css"; mime = "text/css; charset=utf-8"; }
    if (target == "/app.js") { file = "app.js"; mime = "text/javascript; charset=utf-8"; }
    if (target == "/protocol.js") { file = "protocol.js"; mime = "text/javascript; charset=utf-8"; }
    if (file.empty() || request.method() != http::verb::get) {
        http::response<http::string_body> reply{http::status::not_found, request.version()};
        reply.keep_alive(false);
        reply.body() = "Not found";
        reply.prepare_payload();
        http::write(stream, reply);
        return;
    }
    beast::error_code ec;
    http::file_body::value_type body;
    body.open((frontend + "/" + file).c_str(), beast::file_mode::scan, ec);
    if (ec) throw beast::system_error(ec);
    http::response<http::file_body> reply{http::status::ok, request.version()};
    reply.keep_alive(false);
    reply.set(http::field::content_type, mime);
    reply.set(http::field::cache_control, "no-cache");
    reply.body() = std::move(body);
    reply.prepare_payload();
    http::write(stream, reply);
    if constexpr (!std::is_same_v<Stream, tcp::socket>) stream.shutdown(ec);
}
}

void RunServer(World& world) {
    const auto address = setting("ASTEROID_ADDRESS", "127.0.0.1");
    const int port = std::stoi(setting("ASTEROID_PORT", "8080"));
    if (port < 1 || port > 65535) throw std::runtime_error("Invalid ASTEROID_PORT");
    const auto frontend = setting("ASTEROID_FRONTEND", "frontend/browser");
    const auto db_path = setting("ASTEROID_DB", "data/accounts.sqlite3");
    const auto cert = setting("ASTEROID_TLS_CERT", "");
    const auto key = setting("ASTEROID_TLS_KEY", "");
    if (cert.empty() != key.empty()) throw std::runtime_error("Set both ASTEROID_TLS_CERT and ASTEROID_TLS_KEY");
    if (const auto parent = std::filesystem::path(db_path).parent_path(); !parent.empty())
        std::filesystem::create_directories(parent);
    auto accounts = std::make_shared<Accounts>(db_path);
    accounts->LoadPlayers(world);
    auto tls = std::make_shared<ssl::context>(ssl::context::tls_server);
    if (!cert.empty()) {
        tls->set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                         ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
        tls->use_certificate_chain_file(cert);
        tls->use_private_key_file(key, ssl::context::pem);
    }
    net::io_context context;
    tcp::acceptor acceptor(context, {net::ip::make_address(address), static_cast<unsigned short>(port)});
    std::cout << "Web server: " << (cert.empty() ? "http://" : "https://") << address << ':' << port << std::endl;
    for (;;) {
        auto socket = acceptor.accept();
        fcntl(socket.native_handle(), F_SETFD, FD_CLOEXEC);
        // Simple blocking handlers for the initial skeleton: one thread per connection.
        std::thread([socket = std::move(socket), accounts, tls, frontend, &world, secure = !cert.empty()]() mutable {
            try {
                // Rebind the accepted native socket to this connection's event loop.
                net::io_context local_context;
                const auto protocol = socket.local_endpoint().protocol();
                const auto native = socket.release();
                tcp::socket local_socket(local_context, protocol, native);
                if (secure) {
                    ssl::stream<tcp::socket> stream(std::move(local_socket), *tls);
                    stream.handshake(ssl::stream_base::server);
                    serve_http(std::move(stream), *accounts, world, frontend);
                } else {
                    serve_http(std::move(local_socket), *accounts, world, frontend);
                }
            } catch (const std::exception& error) {
                std::cerr << "Connection: " << error.what() << '\n';
            }
        }).detach();
    }
}
