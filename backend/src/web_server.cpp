#include "web_server.hpp"
#include "accounts.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

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

// Future game requests use this ID, never a player ID supplied by the browser.
struct Session { uint32_t user_id = 0; };

std::vector<uint8_t> handle_packet(const std::string& bytes, Session& session, Accounts& accounts) {
    protocol::Reader reader(bytes);
    const auto type = static_cast<protocol::Request>(reader.u8());
    switch (type) {
    case protocol::Request::Login:
    case protocol::Request::Register: {
        const auto credentials = reader.credentials();
        reader.finish();
        if (session.user_id) return protocol::error("Сначала выйдите из аккаунта.");
        auto response = accounts.authenticate(credentials, type == protocol::Request::Register);
        if (response.status == protocol::Status::Ok) session.user_id = response.user_id;
        return protocol::encode(response);
    }
    case protocol::Request::Logout:
        reader.finish();
        session.user_id = 0;
        return {static_cast<uint8_t>(protocol::Response::Logout)};
    default:
        return protocol::error("Неизвестный тип запроса.");
    }
}

template<class Stream>
void serve_websocket(Stream stream, const http::request<http::string_body>& request, Accounts& accounts) {
    websocket::stream<Stream> ws(std::move(stream));
    ws.read_message_max(4096);
    ws.accept(request);
    ws.binary(true);
    Session session;
    for (;;) {
        beast::flat_buffer buffer;
        beast::error_code ec;
        ws.read(buffer, ec);
        if (ec == websocket::error::closed) return;
        if (ec) throw beast::system_error(ec);
        if (!ws.got_binary()) {
            ws.close(websocket::close_code::unknown_data);
            return;
        }
        std::vector<uint8_t> reply;
        try {
            reply = handle_packet(beast::buffers_to_string(buffer.data()), session, accounts);
        } catch (const std::invalid_argument& error) {
            reply = protocol::error(error.what());
        } catch (const std::exception& error) {
            std::cerr << "Request: " << error.what() << '\n';
            reply = protocol::error("Ошибка сервера.");
        }
        ws.write(net::buffer(reply));
    }
}

template<class Stream>
void serve_http(Stream stream, Accounts& accounts, const std::string& frontend) {
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(4096);
    http::read(stream, buffer, parser);
    auto request = parser.release();
    if (request.target() == "/ws" && websocket::is_upgrade(request)) {
        serve_websocket(std::move(stream), request, accounts);
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

void RunServer() {
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
        // Simple blocking handlers for the initial skeleton: one thread per connection.
        std::thread([socket = std::move(socket), accounts, tls, frontend, secure = !cert.empty()]() mutable {
            try {
                if (secure) {
                    ssl::stream<tcp::socket> stream(std::move(socket), *tls);
                    stream.handshake(ssl::stream_base::server);
                    serve_http(std::move(stream), *accounts, frontend);
                } else {
                    serve_http(std::move(socket), *accounts, frontend);
                }
            } catch (const std::exception& error) {
                std::cerr << "Connection: " << error.what() << '\n';
            }
        }).detach();
    }
}
