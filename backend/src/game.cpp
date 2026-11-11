#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/json.hpp>

#include <algorithm>
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
        entity.second->SetPosition(entity.second->GetPosition() + entity.second->GetVelocity() * delta_time);
        entity.second->SetRotation(entity.second->GetRotation() + entity.second->GetAngularVelocity() * delta_time);
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
    std::unique_ptr<Module> mdl;
    std::string name;
    if(type == MODULE_TYPE::PROPULSION)
    {
        name = "prop";
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

void Craft::Send(const json::object& message)
{
    if (!process_ || !process_->running()) {
        return;
    }

    std::string packet = json::serialize(message) + "\n";

    std::lock_guard lock(send_mutex_);

    asio::write(
        process_->get_stdin(),
        asio::buffer(packet)
    );
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
        if (python.empty()) {
            std::cerr << "Python not found\n";
            return;
        }

        // Инициализируем процесс как поле класса
        process_.emplace(io_, python, std::vector<std::string>{"-u", filename});

        // В потоке захватываем this вместо локальных переменных
        reader_ = std::thread([this]() {
            asio::streambuf buffer;

            while (true) {
                boost::system::error_code ec;
                asio::read_until(process_->get_stdout(), buffer, '\n', ec);
                if (ec) break;

                std::istream stream(&buffer);
                std::string line;
                std::getline(stream, line);
                
                try {
                    auto parsed = json::parse(line);

                    if (!parsed.is_object())
                        continue;

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
                        continue;
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
                                id = GetId();
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
                                {"modules", module_names}
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
        });
/*
        Send(json::object{
            {"type", "event"},
            {"name", "enemy_detected"},
            {"data", json::object{{"enemy_id", 42}}}
        });
*/

    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
}


Craft::~Craft()
{
    StopScript();
}

void Craft::StopScript()
{
    // 1. Отправляем команду завершения, если процесс ещё работает
    if (process_ && process_->running()) {
        try {
            Send(json::object{{"type", "shutdown"}});
        } catch (...) {
            // Игнорируем ошибки записи, если Python уже закрыл stdin
        }
    }

    // 2. Закрываем stdin процесса, чтобы Python гарантированно вышел 
    // из ожидания ввода, даже если не обработал JSON "shutdown"
    if (process_) {
        boost::system::error_code ec;
        process_->get_stdin().close(ec);
    }

    // 3. Ждём завершения фонового потока чтения, пока поля класса ещё живы
    if (reader_.joinable()) {
        reader_.join();
    }

    // 4. Дожидаемся полного завершения процесса в ОС (убираем зомби-процесс)
    if (process_) {
        boost::system::error_code ec;
        process_->wait(ec);
        process_.reset();
    }
}

void Craft::Update(float delta_time)
{
    for(auto& mdl : modules_)
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
