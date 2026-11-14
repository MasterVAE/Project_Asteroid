#include "modules.hpp"
#include "game.hpp"
#include "resources.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <iostream>

// prop module


void PropulsionModule::Update(float delta_time)
{
    std::lock_guard lock(parent_->state_mutex_);
    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * throttle_ * delta_time * parent_->Forward());
}

void PropulsionModule::SetThrottle(float throttle)
{
    std::lock_guard lock(parent_->state_mutex_);
    if (throttle >= 0 && throttle <= 1) throttle_ = throttle;
}


boost::json::object PropulsionModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_max_throttle")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            result["max_throttle"] = max_thrust_;
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    if(method == "set_throttle")
    {
        double value = boost::json::value_to<double>(request.at("value"));

        if (std::isfinite(value)) {
            SetThrottle(value);
        }
    }
    return boost::json::object{};
}



// storage module


void StorageModule::ReadyForRecieve()
{
    std::lock_guard lock(parent_->state_mutex_);
    ready_to_recieve_ = true;
}
void StorageModule::NotReadyForRecieve()
{
    std::lock_guard lock(parent_->state_mutex_);
    ready_to_recieve_ = false;
}


std::size_t StorageModule::ResourceCount(RESOURCE_TYPE resource)
{
    const auto it = inventory_.find(resource);
    return it == inventory_.end() ? 0 : it->second;
}

void StorageModule::AddToInventory(std::unordered_map<RESOURCE_TYPE, std::size_t> add)
{
    std::size_t used = 0;

    // Check each count before summing to avoid overflowing std::size_t.
    for (const auto& [resource, count] : inventory_)
    {
        if (count > capacity_ - used)
            throw std::runtime_error("Storage capacity exceeded");
        used += count;
    }
    for (const auto& [resource, count] : add)
    {
        if (count > capacity_ - used)
            throw std::runtime_error("Not enough storage capacity");
        used += count;
    }

    // Commit only after the entire addition succeeds, including allocations.
    auto updated = inventory_;
    for (const auto& [resource, count] : add)
    {
        if (count != 0)
            updated[resource] += count;
    }
    inventory_.swap(updated);
}

void StorageModule::TakeFromInventory(std::unordered_map<RESOURCE_TYPE, std::size_t> take)
{
    for (const auto& [resource, count] : take)
    {
        if (count > ResourceCount(resource))
            throw std::runtime_error("Not enough resources in storage");
    }

    for (const auto& [resource, count] : take)
    {
        if (count == 0)
            continue;

        auto it = inventory_.find(resource);
        it->second -= count;
        if (it->second == 0)
            inventory_.erase(it);
    }
}

void StorageModule::Update(float /*delta_time*/) {}

boost::json::object StorageModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_storage")
    {
        boost::json::array result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            result.reserve(inventory_.size());
            for (const auto& [resource, count] : inventory_)
            {
                const auto* data = std::find_if(RESOURCES, RESOURCES + RESOURCES_COUNT,
                    [type = resource](const RESOURCE_DATA& entry) { return entry.type == type; });
                if (data == RESOURCES + RESOURCES_COUNT)
                    throw std::runtime_error("Unknown resource type");

                result.push_back(boost::json::object{
                    {"resource", data->name},
                    {"count", count}
                });
            }
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    else if(method == "ready_for_recieve")
    {
        ReadyForRecieve();
    }
    else if(method == "not_ready_for_recieve")
    {
        NotReadyForRecieve();
    }
    else if(method == "transfer")
    {
        const auto& value = request.at("value").as_array();
        if (value.size() != 2)
            throw std::invalid_argument("Transfer must contain [id, resource]");

        const auto id = boost::json::value_to<std::size_t>(value.at(0));
        const auto resource_name = boost::json::value_to<std::string>(value.at(1));
        const auto* data = std::find_if(RESOURCES, RESOURCES + RESOURCES_COUNT,
            [&resource_name](const RESOURCE_DATA& entry) { return entry.name == resource_name; });
        if (data == RESOURCES + RESOURCES_COUNT)
            throw std::invalid_argument("Unknown resource type");

        const auto& entities = parent_->world->GetEntities();
        const auto entity = entities.find(id);
        if (entity == entities.end())
            return {};

        if(entity->second->GetType() != ENTITY_TYPE::CRAFT) return {};
        auto* target = static_cast<Craft*>(entity->second.get());
        if (target == nullptr || target == parent_)
            return {};

        const auto modules = target->GetModules();
        std::scoped_lock lock(parent_->state_mutex_, target->state_mutex_);
        if (!(parent_->GetPosition().DistanceSquared(target->GetPosition()) <= 100.0f))
            return {};

        const auto source = inventory_.find(data->type);
        if (source == inventory_.end() || source->second == 0)
            return {};

        for (auto* module : modules)
        {
            if (module->GetType() != MODULE_TYPE::STORAGE)
                continue;

            auto* storage = static_cast<StorageModule*>(module);
            if (!storage->ready_to_recieve_)
                continue;

            std::size_t free_space = storage->capacity_;
            for (const auto& [resource, count] : storage->inventory_)
            {
                if (count >= free_space)
                {
                    free_space = 0;
                    break;
                }
                free_space -= count;
            }
            if (free_space == 0)
                continue;

            storage->AddToInventory({{data->type, 1}});
            if (--source->second == 0)
                inventory_.erase(source);
            break;
        }
    }
    return boost::json::object{};
}

void StorageModule::Transfer(std::size_t id, RESOURCE_TYPE type)
{

}



// RCS module





void RCSModule::Update(float delta_time)
{
    std::lock_guard lock(parent_->state_mutex_);
    parent_->SetAngularVelocity(gyro_ * max_gyro_);
    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * parent_->FromLocalTowWorld(thrust_) * delta_time);
}

void RCSModule::SetGyro(float gyro)
{
    std::lock_guard lock(parent_->state_mutex_);
    if (gyro >= -1 && gyro <= 1) gyro_ = gyro;
}

void RCSModule::SetThrust(Vector2 thrust)
{
    std::lock_guard lock(parent_->state_mutex_);
    if(thrust.Length() <= 1) thrust_ = thrust;
    else thrust_ = thrust.Normalized();
}

boost::json::object RCSModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_max_gyro" || method == "get_max_thrust")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            if(method == "get_max_gyro")
                result["max_gyro"] = max_gyro_;
            else 
                result["max_thrust"] = max_thrust_;
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    if(method == "set_gyro")
    {
        double value = boost::json::value_to<double>(request.at("value"));

        if (std::isfinite(value)) {
            SetGyro(value);
        }
    }
    if (method == "set_thrust")
    {
        const auto& value = request.at("value").as_array();
    
        if (value.size() != 2)
            throw std::invalid_argument("Thrust must contain [x, y]");
    
        float x = boost::json::value_to<float>(value.at(0));
        float y = boost::json::value_to<float>(value.at(1));
    
        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::invalid_argument("Thrust must be finite");
    
        SetThrust(Vector2(x, y));
    }
    return boost::json::object{};
}



// DRILL module

void DrillModule::Update(float delta_time)
{
    const auto modules = parent_->GetModulesOfType(MODULE_TYPE::STORAGE);
    const auto power_modules = parent_->GetModulesOfType(MODULE_TYPE::POWER);
    std::unique_lock lock(parent_->state_mutex_);

    if (!enabled_ || parent_->world == nullptr)
    {
        timer_ = 0;
        return;
    }

    // TakeCharge locks the craft state itself.
    lock.unlock();
    const bool powered = std::any_of(power_modules.begin(), power_modules.end(),
        [this, delta_time](Module* module) {
            return static_cast<PowerModule*>(module)->TakeCharge(power_consuption_ * delta_time);
        });
    lock.lock();

    if (!enabled_ || parent_->world == nullptr)
    {
        timer_ = 0;
        return;
    }
    if (!powered)
        return;

    timer_ += delta_time;
    if (timer_ < drill_cooldown_)
        return;
    timer_ = 0;

    for (const auto& [id, entity] : parent_->world->GetEntities())
    {
        if (entity->GetType() != ENTITY_TYPE::ASTEROID)
            continue;

        const Vector2 diff = entity->GetPosition() - parent_->GetPosition();
        if (diff.Length() >= 1)
            continue;

        auto* asteroid = static_cast<Asteroid*>(entity.get());
        const auto& resources = asteroid->GetResources();

        const RESOURCE_TYPE resource = asteroid->Mine();
        for (auto* module : modules)
        {
            if (module->GetType() != MODULE_TYPE::STORAGE)
                continue;

            auto* storage = static_cast<StorageModule*>(module);
            std::size_t free_space = storage->GetCapacity();
            for (const auto& [type, count] : storage->GetInventory())
            {
                if (count >= free_space)
                {
                    free_space = 0;
                    break;
                }
                free_space -= count;
            }

            if (free_space > 0)
            {
                storage->AddToInventory({{resource, 1}});
                return;
            }
        }

        return;
    }
}

void DrillModule::Enable()
{
    std::lock_guard lock(parent_->state_mutex_);
    enabled_ = true;
}
void DrillModule::Disable()
{
    std::lock_guard lock(parent_->state_mutex_);
    enabled_ = false;
}



boost::json::object DrillModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_drill_cooldown")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            result["drill_cooldown"] = GetDrillCooldown();
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    if(method == "enable") { Enable(); }
    if (method == "disable") { Disable(); }
    return boost::json::object{};
}



// SCAN MODULE



void ScanModule::Update(float /*delta_time*/) {}

std::vector<std::pair<std::string, Vector2>> ScanModule::Scan()
{
    std::vector<std::pair<std::string, Vector2>> result;
    if (parent_->world == nullptr)
        return result;

    const auto power_modules = parent_->GetModulesOfType(MODULE_TYPE::POWER);
    const bool powered = std::any_of(power_modules.begin(), power_modules.end(),
        [this](Module* module) {
            return static_cast<PowerModule*>(module)->TakeCharge(scan_power_);
        });
    if (!powered)
        return result;

    const Vector2 origin = parent_->GetPosition();
    for (const auto& [id, entity] : parent_->world->GetEntities())
    {
        const Vector2 position = entity->GetPosition();
        if (origin.DistanceSquared(position) <= radius * radius)
        {
            const std::string name =
                std::string(entity->GetType() == ENTITY_TYPE::ASTEROID ? "Asteroid #" : "Craft #") +
                std::to_string(id);
            result.emplace_back(name, position);
        }
    }

    return result;
}


boost::json::object ScanModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "scan")
    {
        boost::json::array result;
        for (const auto& [name, position] : Scan())
        {
            result.push_back(boost::json::object{
                {"name", name},
                {"x", position.x},
                {"y", position.y}
            });
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    return boost::json::object{};
}

// POWER MODULE




bool PowerModule::TakeCharge(float charge)
{
    std::lock_guard lock(parent_->state_mutex_);
    if (!std::isfinite(charge) || charge < 0 || charge_ < charge)
        return false;
    charge_ -= charge;
    return true;
}

void PowerModule::Update(float delta_time)
{
    std::lock_guard lock(parent_->state_mutex_);
    charge_ += generation_per_second_ * delta_time;

    if(charge_ > capacity_) charge_ = capacity_;
}

boost::json::object PowerModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_capacity" || method == "get_charge")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            if (method == "get_capacity")
                result["capacity"] = capacity_;
            else
                result["charge"] = charge_;
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    return boost::json::object{};
}
