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
#include <stdexcept>
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
World::World() {
    Start();
}
World::World(std::string /*filename*/) {}

void World::Start() {
    auto shipyard = std::make_unique<Craft>(Vector2(0, 0), "Shipyard");
    shipyard->AddModule(MODULE_TYPE::SHIPYARD);
    shipyard->AddModule(MODULE_TYPE::FACTORY);
    for (int i = 0; i < 6; ++i)
    {
        auto& storage = static_cast<StorageModule&>(shipyard->AddModule(MODULE_TYPE::STORAGE));
        storage.ReadyForRecieve();
    }
    // Ten reactors can sustain the factory (20/s) and shipyard (30/s).
    for (int i = 0; i < 10; ++i)
        shipyard->AddModule(MODULE_TYPE::POWER);
    SpawnEntity(std::move(shipyard));

    auto miner = std::make_unique<Craft>(Vector2(30, 0), "Miner");
    miner->AddModule(MODULE_TYPE::PROPULSION);
    miner->AddModule(MODULE_TYPE::STORAGE);
    miner->AddModule(MODULE_TYPE::RCS);
    miner->AddModule(MODULE_TYPE::DRILL);
    miner->AddModule(MODULE_TYPE::SCANNER);
    miner->AddModule(MODULE_TYPE::POWER);
    auto& fuel_tank = static_cast<FuelTankModule&>(miner->AddModule(MODULE_TYPE::FUEL_TANK));
    fuel_tank.SetFuel(100);
    SpawnEntity(std::move(miner));

    for(int i = 0; i < 50; i++)
    {
        SpawnEntity(std::make_unique<Asteroid>(RandomVector(-1000, 1000)));
    }
}

void World::Restart() {
    entities_.clear();
    pending_spawn_.clear();
    pending_destroy_.clear();
    updating_ = false;
    next_id_ = 0;
    Start();
}

void World::Update(float delta_time) {
    updating_ = true;
    try
    {
        for(auto& entity : entities_)
        {
            if (pending_destroy_.count(entity.first) != 0)
                continue;

            entity.second->Advance(delta_time);
            entity.second->Update(delta_time);
        }
    }
    catch (...)
    {
        updating_ = false;
        throw;
    }
    updating_ = false;

    for (const auto id : pending_destroy_)
        entities_.erase(id);
    pending_destroy_.clear();

    for (auto& entity : pending_spawn_)
    {
        const auto id = entity->GetId();
        entities_.emplace(id, std::move(entity));
    }
    pending_spawn_.clear();
}

void World::SpawnEntity(std::unique_ptr<Entity> entity)
{
    std::size_t id = next_id_++;
    entity->SetId(id);
    entity->SetName(entity->GetName() + " #" + std::to_string(id));
    entity->world = this;
    if (updating_)
        pending_spawn_.push_back(std::move(entity));
    else
        entities_.emplace(id, std::move(entity));
}

void World::DestroyEntity(Entity* entity)
{
    if (entity == nullptr)
        return;

    const auto it = entities_.find(entity->GetId());
    if (it == entities_.end() || it->second.get() != entity)
        return;

    // Keep entities and iterators alive until the current update finishes.
    if (updating_)
        pending_destroy_.insert(it->first);
    else
        entities_.erase(it);
}


// Asteroid
Asteroid::Asteroid() : Asteroid(Vector2(0)) {}
Asteroid::Asteroid(Vector2 pos, std::string name)
    : Entity(ENTITY_TYPE::ASTEROID, pos, Vector2(0), 0, 0, std::move(name))
{
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> count_dist(1, 10);
    std::uniform_int_distribution<std::size_t> type_dist(0, RAW_RESOURCES_COUNT - 1);

    const int count = count_dist(gen);
    for (int i = 0; i < count; ++i)
    {
        ++resources_[RESOURCES[type_dist(gen)].type];
    }
}

void Asteroid::Update(float) {}

RESOURCE_TYPE Asteroid::Mine()
{
    std::size_t resource_count = 0;
    for (const auto& resource : resources_)
    {
        resource_count += resource.second;
    }

    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<std::size_t> dist(0, resource_count - 1);
    std::size_t index = dist(gen);

    auto resource = resources_.begin();
    while (index >= resource->second)
    {
        index -= resource->second;
        ++resource;
    }

    const RESOURCE_TYPE type = resource->first;
    if (--resource->second == 0)
    {
        resources_.erase(resource);
    }

    resource_count--;
    if (resource_count == 0) 
    { 
        world->DestroyEntity(this); 
    }


    return type;
}


// Craft
Craft::Craft() : Craft(Vector2(0)) {}
Craft::Craft(Vector2 pos, std::string name)
    : Entity(ENTITY_TYPE::CRAFT, pos, Vector2(0), 0, 0, std::move(name)) {}

void Craft::RecalculateMass()
{
    std::lock_guard lock(state_mutex_);
    RecalculateMassLocked();
}

void Craft::RecalculateMassLocked()
{
    mass_ = 0;

    for (const auto& module : modules_)
    {
        for (const auto& data : MODULES_DATA)
        {
            if (data.type == module->GetType())
            {
                mass_ += data.mass;
                break;
            }
        }

        if (module->GetType() == MODULE_TYPE::STORAGE)
        {
            auto* storage = static_cast<StorageModule*>(module.get());
            for (const auto& [resource, count] : storage->GetInventory())
            {
                for (const auto& data : RESOURCES)
                {
                    if (data.type == resource)
                    {
                        mass_ += data.mass * count;
                        break;
                    }
                }
            }
        }
    }
}

Module& Craft::AddModule(MODULE_TYPE type)
{
    std::lock_guard lock(state_mutex_);
    return AddModuleLocked(type);
}

Module& Craft::AddModuleLocked(MODULE_TYPE type)
{
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
    else if(type == MODULE_TYPE::STORAGE)
    {
        mdl = std::make_unique<StorageModule>(this, 20);
    }
    else if(type == MODULE_TYPE::RCS)
    {
        mdl = std::make_unique<RCSModule>(this);
    }
    else if(type == MODULE_TYPE::DRILL)
    {
        mdl = std::make_unique<DrillModule>(this);
    }
    else if(type == MODULE_TYPE::SCANNER)
    {
        mdl = std::make_unique<ScanModule>(this);
    }
    else if(type == MODULE_TYPE::POWER)
    {
        mdl = std::make_unique<PowerModule>(this);
    }
    else if(type == MODULE_TYPE::FACTORY)
    {
        mdl = std::make_unique<FactoryModel>(this);
    }
    else if(type == MODULE_TYPE::SHIPYARD)
    {
        mdl = std::make_unique<ShipyardModule>(this);
    }
    else if(type == MODULE_TYPE::FUEL_TANK)
    {
        mdl = std::make_unique<FuelTankModule>(this);
    }
    else
    {
        throw std::invalid_argument("Unknown module type");
    }

    auto& next_index = module_indices_[type];
    name += "_" + std::to_string(next_index);
    mdl->SetName(name);
    modules_.push_back(std::move(mdl));
    ++next_index;

    RecalculateMassLocked();

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

std::vector<Module*> Craft::GetModulesOfType(MODULE_TYPE type)
{
    std::lock_guard lock(state_mutex_);
    std::vector<Module*> modules;
    for (const auto& module : modules_)
    {
        if (module->GetType() == type)
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

bool Craft::IsScriptRunning()
{
    std::lock_guard lock(send_mutex_);
    boost::system::error_code ec;
    return !stopping_ && process_ && process_->running(ec) && !ec;
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

        process_.emplace(io_, python, std::vector<std::string>{"-X", "utf8", "-u", filename});

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
            if (ec) {
                // Preserve a final stdout fragment without a trailing newline.
                if (read_buffer_.size() > 0) {
                    std::cerr << &read_buffer_ << std::flush;
                }
                return;
            }

            std::istream stream(&read_buffer_);
            std::string line;
            std::getline(stream, line);
            HandlePacket(line);
            ReadPacket();
        });
}

void Craft::HandlePacket(const std::string& line)
{
    // Scripts may print before importing api, or write directly to stdout.
    // Only API requests/commands are protocol packets; everything else is text.
    boost::system::error_code parse_error;
    auto parsed = json::parse(line, parse_error);
    if (parse_error || !parsed.is_object()) {
        std::cerr << line << '\n';
        return;
    }
    const auto& msg = parsed.as_object();
    const auto* type_value = msg.if_contains("type");
    if (!type_value || !type_value->is_string() ||
        (type_value->as_string() != "request" && type_value->as_string() != "command")) {
        std::cerr << line << '\n';
        return;
    }
    try {
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
            else if (method == "get_name") {
                response["result"] = json::object{{"name", GetName()}};
            }
            else if (method == "get_mass") {
                float mass;

                {
                    std::lock_guard lock(state_mutex_);
                    mass = GetMass();
                }

                response["result"] = json::object{
                    {"mass", mass},
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

                response["result"] = json::object{
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
            const auto method = json::value_to<std::string>(msg.at("method"));
            if (method == "set_name")
                SetName(json::value_to<std::string>(msg.at("value")));
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
    RecalculateMass();
    for (auto* mdl : GetModules())
    {
        mdl->Update(delta_time);
    }
}



// Entity

Entity::Entity(ENTITY_TYPE type, std::string name)
    : Entity(type, Vector2(0), Vector2(0), 0, 0, std::move(name)) {}
Entity::Entity(ENTITY_TYPE type, Vector2 pos, Vector2 vel, float rot, float a_vel, std::string name)
    : position_(pos), velocity_(vel), rotation_(rot), angular_velocity_(a_vel), type_(type),
      name_(name.find_first_not_of(" \t\r\n") == std::string::npos
          ? (type == ENTITY_TYPE::ASTEROID ? "Asteroid" : "Craft") : std::move(name)) {}

Vector2 Entity::Forward()
{
    return Vector2(std::cos(GetRotation()), std::sin(GetRotation()));
}

Vector2 Entity::FromLocalTowWorld(Vector2 local)
{
    return local.Rotated(GetRotation());
}

void Entity::SetName(std::string name)
{
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::invalid_argument("Entity name must not be empty");
    std::lock_guard lock(entity_mutex_);
    name_ = std::move(name);
}
std::string Entity::GetName() const
{
    std::lock_guard lock(entity_mutex_);
    return name_;
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
    while (rotation_ < 0) rotation_ += 2 * pi;
    while (rotation_ >= 2 * pi) rotation_ -= 2 * pi;
}
