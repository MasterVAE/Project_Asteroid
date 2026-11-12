#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/json.hpp>

#include <algorithm>
#include <cerrno>
#include <csignal>
#ifndef _WIN32
#include <fcntl.h>
#endif
#include <system_error>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <string>
#include <iostream>
#include <random>

#include "game.hpp"
#include "vector.hpp"
#include "modules.hpp"

namespace asio = boost::asio;
namespace bp = boost::process::v2;
namespace json = boost::json;

using namespace std::chrono_literals;

namespace {
constexpr double pi = 3.14159265358979323846;
}

float RandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

Vector2 RandomVector(float min, float max)
{
    return Vector2(RandomFloat(min ,max), RandomFloat(min, max));
}






// World
World::World() : next_id_(0) {
    SpawnEntity(std::make_unique<Craft>(Vector2(0)));

    auto it = entities_.begin();
    Entity& entity = *(it->second);
    Craft& craft = static_cast<Craft&>(entity);
    craft.AddModule(MODULE_TYPE::PROPULSION);
    craft.LaunchScript("scripts/script.py");

}
World::World(std::string filename) {}
void World::Update(float delta_time) {
    for(auto& entity : entities_)
    {
        entity.second->Advance(delta_time);
        entity.second->Update(delta_time);
    }
}

void World::SpawnEntity(std::unique_ptr<Entity> entity)
{
    std::size_t id = next_id_++;
    entity->SetId(id);
    entities_.emplace(id, std::move(entity));
}




// Craft
Craft::Craft() : Entity(), module_index_(0) {}
Craft::Craft(Vector2 pos) : Entity(pos, Vector2(0), 0, 0), module_index_(0) {}

Module& Craft::AddModule(MODULE_TYPE type)
{
    std::lock_guard lock(state_mutex_);
    std::unique_ptr<Module> mdl;
    std::string name;

    for(size_t i = 0; i < MODULES_COUNT; i++)
    {
        if(MODULES_DATA[i].type == type)
        {
            name = MODULES_DATA[i].name;
            break;
        }
    }

    if(type == MODULE_TYPE::PROPULSION)
    {
        mdl = std::make_unique<PropulsionModule>(this);
    }
    else
    {
        throw std::invalid_argument("Unknown module type");
    }

    name += std::to_string(module_index_++);
    mdl->SetName(name);
    modules_.push_back(std::move(mdl));

    return *modules_.back();
}

std::vector<Module*> Craft::GetModules()
{
    std::lock_guard lock(state_mutex_);
    std::vector<Module*> modules;
    modules.reserve(modules_.size());
    for (auto& module : modules_) {
        modules.push_back(module.get());
    }
    return modules;
}

void Craft::Send(const json::object& message)
{
    std::lock_guard lock(send_mutex_);
    if (stopping_ || !process_) return;

    asio::post(io_, [this, packet = json::serialize(message) + "\n"]() mutable {
        QueuePacket(std::move(packet));
    });
}

void Craft::QueuePacket(std::string packet)
{
    if (!process_->get_stdin().is_open()) return;

    const bool idle = outgoing_.empty();
    outgoing_.push_back(std::move(packet));
    if (idle) WritePacket();
}

void Craft::WritePacket()
{
    asio::async_write(process_->get_stdin(), asio::buffer(outgoing_.front()),
        [this](boost::system::error_code ec, std::size_t) {
            if (ec) {
                outgoing_.clear();
                process_->get_stdin().close(ec);
                return;
            }
            outgoing_.pop_front();
            if (!outgoing_.empty()) {
                WritePacket();
            } else if (close_stdin_when_idle_) {
                process_->get_stdin().close(ec);
            }
        });
}

void Craft::SendEvent(const json::object& event_data)
{
    Send(event_data);
}

void Craft::LaunchScript(std::string filename)
{
    StopScript();

    try {
        auto python = bp::environment::find_executable("python3");
#ifdef _WIN32
        if (python.empty()) python = bp::environment::find_executable("python");
#endif
        if (python.empty()) {
            std::cerr << "Python not found\n";
            return;
        }

        process_.emplace(io_, python, std::vector<std::string>{"-u", filename});

        // On macOS suppress SIGPIPE only for this pipe. Elsewhere let writes
        // report broken_pipe instead of terminating the application.
#if defined(F_SETNOSIGPIPE)
        if (::fcntl(process_->get_stdin().native_handle(), F_SETNOSIGPIPE, 1) == -1) {
            throw std::system_error(errno, std::generic_category(), "F_SETNOSIGPIPE");
        }
#elif !defined(_WIN32)
        static std::once_flag ignore_sigpipe;
        std::call_once(ignore_sigpipe, [] {
            if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
                throw std::system_error(errno, std::generic_category(), "SIGPIPE");
            }
        });
#endif

        io_.restart();
        work_.emplace(asio::make_work_guard(io_));
        close_stdin_when_idle_ = false;
        ReadPacket();
        {
            std::lock_guard lock(send_mutex_);
            stopping_ = false;
        }
        reader_ = std::thread([this] { io_.run(); });
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        StopScript();
    }
}

void Craft::ReadPacket()
{
    asio::async_read_until(process_->get_stdout(), read_buffer_, '\n',
        [this](boost::system::error_code ec, std::size_t) {
            if (ec) return;

            std::istream stream(&read_buffer_);
            std::string line;
            std::getline(stream, line);
            HandlePacket(line);
            ReadPacket();
        });
}

void Craft::HandlePacket(const std::string& line)
{
    try {
        auto parsed = json::parse(line);

        if (!parsed.is_object())
            return;

        const auto& msg = parsed.as_object();

        std::string module =
            json::value_to<std::string>(
                msg.at("module")
            );

        if(module != "none")
        {
            for(auto& mdl : GetModules())
            {
                if(mdl->GetName() == module)
                {
                    Send(mdl->Handle(msg));
                }
            }
            return;
        }


        std::string type =
            json::value_to<std::string>(
                msg.at("type")
            );


        // RPC-запрос из Python
        if (type == "request") {
            auto id = msg.at("id").as_int64();

            std::string method =
                json::value_to<std::string>(
                    msg.at("method")
                );

            json::object response{
                {"type", "response"},
                {"id", id}
            };




            if (method == "get_id") {
                std::size_t craft_id;

                {
                    std::lock_guard lock(state_mutex_);
                    craft_id = GetId();
                }

                response["result"] = json::object{
                    {"craft_id", craft_id},
                };
            }
            else if (method == "get_position") {
                Vector2 pos;

                {
                    std::lock_guard lock(state_mutex_);
                    pos = GetPosition();
                }

                response["result"] = json::object{
                    {"x", pos.x},
                    {"y", pos.y}
                };
            }
            else if(method == "get_velocity")
            {
                Vector2 vel;

                {
                    std::lock_guard lock(state_mutex_);
                    vel = GetVelocity();
                }

                response["result"] = json::object{
                    {"x", vel.x},
                    {"y", vel.y}
                };
            }
            else if(method == "get_angle")
            {
                float angle;

                {
                    std::lock_guard lock(state_mutex_);
                    angle = GetRotation();
                }

                response["result"] = json::object{
                    {"angle", angle}
                };
            }
            else if(method == "get_angular_velocity")
            {
                float a_vel;

                {
                    std::lock_guard lock(state_mutex_);
                    a_vel = GetAngularVelocity();
                }

                response["result"] = json::object{
                    {"angular_velocity", a_vel}
                };
            }
            else if(method == "get_modules")
            {
                std::vector<std::string> module_names;

                {
                    std::lock_guard lock(state_mutex_);
                    for(auto& module : modules_)
                    {
                        module_names.push_back(module->GetName());
                    }
                }

                response["result"] = json::array{
                    {"modules", json::value_from(module_names)}
                };
            }
            else {
                response["error"] =
                    "Unknown RPC method";
            }

            Send(response);
        }

        // Команда управления кораблём
        else if (type == "command") {
            std::string name =
                json::value_to<std::string>(
                    msg.at("name")
                );



        }

    } catch (const std::exception& e) {
        std::cerr
            << "Invalid packet: "
            << e.what() << '\n';
    }
}


Craft::~Craft()
{
    StopScript();
}

void Craft::StopScript()
{
    {
        std::lock_guard lock(send_mutex_);
        stopping_ = true;
    }
    if (!process_) return;

    if (reader_.joinable()) {
        asio::post(io_, [this] {
            QueuePacket(json::serialize(json::object{{"type", "shutdown"}}) + "\n");
            close_stdin_when_idle_ = true;
            if (outgoing_.empty()) {
                boost::system::error_code ec;
                process_->get_stdin().close(ec);
            }
        });
    }

    // Allow cooperative scripts to exit, but never wait indefinitely for them.
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    boost::system::error_code ec;
    while (process_->running(ec) && !ec && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    if (ec || process_->running(ec)) {
        process_->terminate(ec);
    }
    process_->wait(ec);

    auto close_pipes = [this] {
        boost::system::error_code ignored;
        process_->get_stdin().close(ignored);
        process_->get_stdout().close(ignored);
        work_.reset();
    };
    if (reader_.joinable()) {
        // Cancel I/O on its own thread, including pipes inherited by descendants.
        asio::post(io_, close_pipes);
        reader_.join();
    } else {
        close_pipes();
        // Drain cancellations if creating the I/O thread failed during launch.
        io_.restart();
        io_.run();
    }

    outgoing_.clear();
    read_buffer_.consume(read_buffer_.size());
    process_.reset();
}

void Craft::Update(float delta_time)
{
    for (auto* mdl : GetModules())
    {
        mdl->Update(delta_time);
    }
}



// Entity

Entity::Entity() : position_(0), velocity_(0), rotation_(0), angular_velocity_(0) {}
Entity::Entity(Vector2 pos, Vector2 vel, float rot, float a_vel)
                    : position_(pos), velocity_(vel), rotation_(rot), angular_velocity_(a_vel) {}

Vector2 Entity::Forward()
{
    return Vector2(std::cos(GetRotation()), std::sin(GetRotation()));
}

std::size_t Entity::GetId() const
{
    std::lock_guard lock(entity_mutex_);
    return id_;
}

void Entity::SetId(std::size_t id)
{
    std::lock_guard lock(entity_mutex_);
    id_ = id;
}

Vector2 Entity::GetPosition() const
{
    std::lock_guard lock(entity_mutex_);
    return position_;
}

void Entity::SetPosition(const Vector2& pos)
{
    std::lock_guard lock(entity_mutex_);
    position_ = pos;
}

Vector2 Entity::GetVelocity() const
{
    std::lock_guard lock(entity_mutex_);
    return velocity_;
}

void Entity::SetVelocity(const Vector2& vel)
{
    std::lock_guard lock(entity_mutex_);
    velocity_ = vel;
}

float Entity::GetRotation() const
{
    std::lock_guard lock(entity_mutex_);
    return rotation_;
}

void Entity::SetRotation(float rotation)
{
    std::lock_guard lock(entity_mutex_);
    rotation_ = rotation;
    if (rotation_ < 0) rotation_ += 2 * pi;
    else if (rotation_ >= 2 * pi) rotation_ -= 2 * pi;
}

float Entity::GetAngularVelocity() const
{
    std::lock_guard lock(entity_mutex_);
    return angular_velocity_;
}

void Entity::SetAngularVelocity(float a_vel)
{
    std::lock_guard lock(entity_mutex_);
    angular_velocity_ = a_vel;
}

void Entity::Advance(float delta_time)
{
    std::lock_guard lock(entity_mutex_);
    position_ += velocity_ * delta_time;
    rotation_ += angular_velocity_ * delta_time;
    if (rotation_ < 0) rotation_ += 2 * pi;
    else if (rotation_ >= 2 * pi) rotation_ -= 2 * pi;
}
