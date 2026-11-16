#include "modules.hpp"
#include "game.hpp"
#include "resources.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <utility>

// prop module


void PropulsionModule::Update(float delta_time)
{
    if (!std::isfinite(delta_time) || delta_time <= 0)
        return;

    const auto fuel_tanks = parent_->GetModulesOfType(MODULE_TYPE::FUEL_TANK);
    std::lock_guard lock(parent_->state_mutex_);
    if (throttle_ == 0)
        return;

    const float fuel = fuel_consumption_per_second_ * throttle_ * delta_time;
    const bool fueled = std::any_of(fuel_tanks.begin(), fuel_tanks.end(),
        [fuel](Module* module) {
            return static_cast<FuelTankModule*>(module)->GetFuelLocked(fuel);
        });
    if (!fueled)
    {
        throttle_ = 0;
        return;
    }

    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * throttle_ * delta_time * parent_->Forward() / parent_->GetMass());
}

void PropulsionModule::SetThrottle(float throttle)
{
    std::lock_guard lock(parent_->state_mutex_);
    if (throttle >= 0 && throttle <= 1) throttle_ = throttle;
}

float PropulsionModule::GetThrottle()
{
    std::lock_guard lock(parent_->state_mutex_);
    return throttle_;
}


boost::json::object PropulsionModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_max_throttle" || method == "get_fuel_consumption")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            if (method == "get_max_throttle")
                result["max_throttle"] = max_thrust_;
            else
                result["fuel_consumption"] = fuel_consumption_per_second_;
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
    else if(method == "ready_to_recieve")
    {
        ReadyForRecieve();
    }
    else if(method == "not_ready_to_recieve")
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




// RCS module





void RCSModule::Update(float delta_time)
{
    std::lock_guard lock(parent_->state_mutex_);
    parent_->SetAngularVelocity(gyro_ * max_gyro_ / parent_->GetMass());
    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * parent_->FromLocalTowWorld(thrust_) * delta_time / parent_->GetMass());
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
        if (diff.LengthSquared() > 10.0f * 10.0f)
            continue;

        auto* asteroid = static_cast<Asteroid*>(entity.get());


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



// SCANNER MODULE



void ScanModule::Update(float /*delta_time*/) {}

std::vector<ScanModule::Result> ScanModule::Scan()
{
    std::vector<Result> result;
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
            result.push_back({id, entity->GetName(),
                entity->GetType() == ENTITY_TYPE::ASTEROID ? "asteroid" : "craft", position});
        }
    }

    return result;
}


void ScanModule::Signal(std::size_t id, const std::string& message)
{
    if (parent_->world == nullptr)
        return;

    const auto& entities = parent_->world->GetEntities();
    const auto target = entities.find(id);
    if (target == entities.end() || target->second->GetType() != ENTITY_TYPE::CRAFT)
        return;

    static_cast<Craft*>(target->second.get())->SendEvent({
        {"type", "event"},
        {"name", "signal"},
        {"data", boost::json::object{
            {"sender_id", parent_->GetId()},
            {"message", message}
        }}
    });
}

boost::json::object ScanModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "scan")
    {
        boost::json::array result;
        for (const auto& entry : Scan())
        {
            result.push_back(boost::json::object{
                {"id", entry.id},
                {"name", entry.name},
                {"type", entry.type},
                {"x", entry.position.x},
                {"y", entry.position.y}
            });
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    else if (method == "signal")
    {
        const auto& value = request.at("value").as_array();
        if (value.size() != 2)
            throw std::invalid_argument("Signal must contain [id, message]");

        const auto id = boost::json::value_to<std::size_t>(value.at(0));
        const auto message = boost::json::value_to<std::string>(value.at(1));
        Signal(id, message);
    }
    return boost::json::object{};
}

// POWER MODULE




bool PowerModule::TakeCharge(float charge)
{
    std::lock_guard lock(parent_->state_mutex_);
    return TakeChargeLocked(charge);
}

bool PowerModule::TakeChargeLocked(float charge)
{
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



// FACTORY MODULE


const std::vector<RECIPE>& FactoryModel::GetRecipes() const
{
    static const std::vector<RECIPE> recipes(RECIPES, RECIPES + RECIPES_COUNT);
    return recipes;
}

void FactoryModel::Produce(RECIPE recipe)
{
    std::lock_guard lock(parent_->state_mutex_);
    queue_.push_back(std::move(recipe));
}

void FactoryModel::Update(float delta_time)
{
    if (!std::isfinite(delta_time) || delta_time <= 0)
        return;

    const auto storage_modules = parent_->GetModulesOfType(MODULE_TYPE::STORAGE);
    const auto power_modules = parent_->GetModulesOfType(MODULE_TYPE::POWER);
    std::lock_guard lock(parent_->state_mutex_);

    while (!queue_.empty())
    {
        // Stage the whole conversion so blocked orders cannot consume resources.
        std::vector<std::unordered_map<RESOURCE_TYPE, std::size_t>> inventories;
        inventories.reserve(storage_modules.size());
        for (auto* module : storage_modules)
            inventories.push_back(static_cast<StorageModule*>(module)->GetInventory());

        const auto& recipe = queue_.front();
        for (const auto resource : recipe.input)
        {
            bool found = false;
            for (auto& inventory : inventories)
            {
                const auto it = inventory.find(resource);
                if (it == inventory.end() || it->second == 0)
                    continue;
                if (--it->second == 0)
                    inventory.erase(it);
                found = true;
                break;
            }
            if (!found)
                return;
        }

        std::vector<std::size_t> free_space;
        free_space.reserve(storage_modules.size());
        for (std::size_t i = 0; i < storage_modules.size(); ++i)
        {
            std::size_t space = static_cast<StorageModule*>(storage_modules[i])->GetCapacity();
            for (const auto& [resource, count] : inventories[i])
            {
                if (count > space)
                    return;
                space -= count;
            }
            free_space.push_back(space);
        }
        for (const auto resource : recipe.output)
        {
            const auto slot = std::find_if(free_space.begin(), free_space.end(),
                [](std::size_t space) { return space > 0; });
            if (slot == free_space.end())
                return;
            ++inventories[static_cast<std::size_t>(slot - free_space.begin())][resource];
            --*slot;
        }

        const float elapsed = std::min(delta_time, production_cooldown_ - time_);
        // Only advance production after paying for this interval of work.
        // Keep the craft lock held so other consumers cannot take this charge.
        const bool powered = std::any_of(power_modules.begin(), power_modules.end(),
            [this, elapsed](Module* module) {
                return static_cast<PowerModule*>(module)->TakeChargeLocked(
                    energy_consuption_per_second_ * elapsed);
            });
        if (!powered)
            return;

        time_ += elapsed;
        delta_time -= elapsed;
        if (time_ < production_cooldown_)
            return;

        for (std::size_t i = 0; i < storage_modules.size(); ++i)
            static_cast<StorageModule*>(storage_modules[i])->GetInventory().swap(inventories[i]);
        queue_.pop_front();
        time_ = 0;
        if (delta_time <= 0)
            return;
    }
}

boost::json::object FactoryModel::Handle(boost::json::object request)
{
    const auto method = boost::json::value_to<std::string>(request.at("method"));
    if (method == "get_recipes" || method == "get_queue")
    {
        std::vector<RECIPE> recipes;
        if (method == "get_queue")
        {
            std::lock_guard lock(parent_->state_mutex_);
            recipes.assign(queue_.begin(), queue_.end());
        }
        else
            recipes = GetRecipes();

        const auto resource_name = [](RESOURCE_TYPE resource) -> const std::string& {
            const auto* data = std::find_if(RESOURCES, RESOURCES + RESOURCES_COUNT,
                [resource](const RESOURCE_DATA& entry) { return entry.type == resource; });
            if (data == RESOURCES + RESOURCES_COUNT)
                throw std::runtime_error("Unknown resource type");
            return data->name;
        };

        boost::json::array result;
        result.reserve(recipes.size());
        for (const auto& recipe : recipes)
        {
            boost::json::array input;
            boost::json::array output;
            for (const auto resource : recipe.input)
                input.push_back(boost::json::value(resource_name(resource)));
            for (const auto resource : recipe.output)
                output.push_back(boost::json::value(resource_name(resource)));
            result.push_back(boost::json::object{{"input", input}, {"output", output}});
        }
        return {{"type", "response"}, {"id", request.at("id")}, {"result", result}};
    }
    if (method == "produce")
    {
        const auto& value = request.at("value");
        if ((!value.is_int64() && !value.is_uint64()) ||
            (value.is_int64() && value.as_int64() < 0))
            throw std::invalid_argument("Recipe index must be a non-negative integer");
        const auto index = boost::json::value_to<std::size_t>(value);
        if (index >= GetRecipes().size())
            throw std::out_of_range("Recipe index out of range");
        Produce(GetRecipes()[index]);
    }
    return {};
}



// SHIPYARD MODULE



void ShipyardModule::ConstructCraft()
{
    std::lock_guard lock(parent_->state_mutex_);
    queue_.push_back({0, MODULE_TYPE::NONE});
}
void ShipyardModule::AddModule(std::size_t craft_id, MODULE_TYPE type)
{
    const auto* recipe = std::find_if(MODULE_RECIPES, MODULE_RECIPES + MODULE_RECIPES_COUNT,
        [type](const CONSTRUCTION_RECIPE& entry) { return entry.output == type; });
    if (recipe == MODULE_RECIPES + MODULE_RECIPES_COUNT)
        throw std::invalid_argument("Unknown module type");

    std::lock_guard lock(parent_->state_mutex_);
    queue_.push_back({craft_id, type});
}

void ShipyardModule::Update(float delta_time)
{
    if (!std::isfinite(delta_time) || delta_time <= 0 || parent_->world == nullptr)
        return;

    while (delta_time > 0)
    {
        const auto storages = parent_->GetModulesOfType(MODULE_TYPE::STORAGE);
        const auto powers = parent_->GetModulesOfType(MODULE_TYPE::POWER);
        std::unique_lock lock(parent_->state_mutex_);
        if (queue_.empty())
            return;

        const auto order = queue_.front();
        const CONSTRUCTION_RECIPE* recipe = &CRAFT_RECIPE;
        Craft* target = nullptr;
        std::unique_lock<std::mutex> target_lock;
        if (order.type != MODULE_TYPE::NONE)
        {
            const auto& entities = parent_->world->GetEntities();
            const auto it = entities.find(order.craft_id);
            if (it == entities.end() || it->second->GetType() != ENTITY_TYPE::CRAFT)
            {
                // Discard an impossible target without blocking subsequent orders.
                std::cerr << "Shipyard: target craft not found: " << order.craft_id << '\n';
                queue_.pop_front();
                time_ = 0;
                continue;
            }
            target = static_cast<Craft*>(it->second.get());
            if (target != parent_)
            {
                target_lock = std::unique_lock<std::mutex>(target->state_mutex_, std::defer_lock);
                lock.unlock();
                std::lock(lock, target_lock);
            }
            if (!(parent_->GetPosition().DistanceSquared(target->GetPosition()) <=
                    building_radius_ * building_radius_))
                return;

            recipe = std::find_if(MODULE_RECIPES, MODULE_RECIPES + MODULE_RECIPES_COUNT,
                [order](const CONSTRUCTION_RECIPE& entry) { return entry.output == order.type; });
            if (recipe == MODULE_RECIPES + MODULE_RECIPES_COUNT)
                throw std::logic_error("Unknown module in shipyard queue");
        }

        // Prepare all withdrawals before consuming energy or changing any storage.
        std::vector<std::unordered_map<RESOURCE_TYPE, std::size_t>> inventories;
        inventories.reserve(storages.size());
        for (auto* module : storages)
            inventories.push_back(static_cast<StorageModule*>(module)->GetInventory());
        for (const auto resource : recipe->input)
        {
            bool found = false;
            for (auto& inventory : inventories)
            {
                const auto it = inventory.find(resource);
                if (it == inventory.end() || it->second == 0)
                    continue;
                if (--it->second == 0)
                    inventory.erase(it);
                found = true;
                break;
            }
            if (!found)
                return;
        }

        const float elapsed = std::min(delta_time, production_cooldown_ - time_);
        const bool powered = std::any_of(powers.begin(), powers.end(),
            [this, elapsed](Module* module) {
                return static_cast<PowerModule*>(module)->TakeChargeLocked(
                    energy_consuption_per_second_ * elapsed);
            });
        if (!powered)
            return;

        time_ += elapsed;
        delta_time -= elapsed;
        if (time_ < production_cooldown_)
            return;

        if (target != nullptr)
        {
            target->AddModuleLocked(order.type);
        }
        else
        {
            auto craft = std::make_unique<Craft>(
                parent_->GetPosition() + parent_->Forward() * (building_radius_ * 0.5f));
            craft->SetVelocity(parent_->GetVelocity());
            craft->SetRotation(parent_->GetRotation());
            parent_->world->SpawnEntity(std::move(craft));
        }

        for (std::size_t i = 0; i < storages.size(); ++i)
            static_cast<StorageModule*>(storages[i])->GetInventory().swap(inventories[i]);
        parent_->RecalculateMassLocked();
        queue_.pop_front();
        time_ = 0;
    }
}

boost::json::object ShipyardModule::Handle(boost::json::object request)
{
    const auto method = boost::json::value_to<std::string>(request.at("method"));
    if (method == "get_queue")
    {
        boost::json::array result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            result.reserve(queue_.size());
            for (const auto& order : queue_)
            {
                boost::json::object entry{{"output", "craft"}, {"craft_id", nullptr}};
                if (order.type != MODULE_TYPE::NONE)
                {
                    const auto* module = std::find_if(MODULES_DATA, MODULES_DATA + MODULES_COUNT,
                        [&order](const MODULE_DATA& data) { return data.type == order.type; });
                    if (module == MODULES_DATA + MODULES_COUNT)
                        throw std::logic_error("Unknown module in shipyard queue");
                    entry["output"] = module->name;
                    entry["craft_id"] = order.craft_id;
                }
                result.push_back(std::move(entry));
            }
        }
        return {{"type", "response"}, {"id", request.at("id")}, {"result", result}};
    }
    else if (method == "construct_craft")
    {
        ConstructCraft();
    }
    else if (method == "add_module")
    {
        const auto& value = request.at("value").as_array();
        if (value.size() != 2)
            throw std::invalid_argument("Module construction must contain [craft_id, module_name]");
        const auto& id = value.at(0);
        if ((!id.is_int64() && !id.is_uint64()) || (id.is_int64() && id.as_int64() < 0))
            throw std::invalid_argument("Craft ID must be a non-negative integer");
        const auto craft_id = boost::json::value_to<std::size_t>(id);
        const auto name = boost::json::value_to<std::string>(value.at(1));
        const auto* module = std::find_if(MODULES_DATA, MODULES_DATA + MODULES_COUNT,
            [&name](const MODULE_DATA& entry) { return entry.name == name; });
        if (module == MODULES_DATA + MODULES_COUNT)
            throw std::invalid_argument("Unknown module name");
        AddModule(craft_id, module->type);
    }
    else if (method == "get_recipes")
    {
        const auto serialize = [](const CONSTRUCTION_RECIPE& recipe) {
            boost::json::array input;
            for (const auto resource : recipe.input)
            {
                const auto* data = std::find_if(RESOURCES, RESOURCES + RESOURCES_COUNT,
                    [resource](const RESOURCE_DATA& entry) { return entry.type == resource; });
                if (data == RESOURCES + RESOURCES_COUNT)
                    throw std::logic_error("Unknown construction resource");
                input.push_back(boost::json::value(data->name));
            }
            std::string output = "craft";
            if (recipe.output != MODULE_TYPE::NONE)
            {
                const auto* data = std::find_if(MODULES_DATA, MODULES_DATA + MODULES_COUNT,
                    [&recipe](const MODULE_DATA& entry) { return entry.type == recipe.output; });
                if (data == MODULES_DATA + MODULES_COUNT)
                    throw std::logic_error("Unknown construction module");
                output = data->name;
            }
            return boost::json::object{{"input", input}, {"output", output}};
        };
        boost::json::array result;
        result.push_back(serialize(CRAFT_RECIPE));
        for (const auto& recipe : MODULE_RECIPES)
            result.push_back(serialize(recipe));
        return {{"type", "response"}, {"id", request.at("id")}, {"result", result}};
    }
    return {};
}




// FUEL MODULE


bool FuelTankModule::GetFuel(float fuel)
{
    std::lock_guard lock(parent_->state_mutex_);
    return GetFuelLocked(fuel);
}

void FuelTankModule::SetFuel(float fuel)
{
    std::lock_guard lock(parent_->state_mutex_);
    fuel_ = fuel;
}

bool FuelTankModule::GetFuelLocked(float fuel)
{
    if (!std::isfinite(fuel) || fuel < 0 || fuel_ < fuel)
        return false;

    fuel_ -= fuel;
    return true;
}

void FuelTankModule::Update(float /*delta_time*/)
{
    const auto storage_modules = parent_->GetModulesOfType(MODULE_TYPE::STORAGE);
    std::lock_guard lock(parent_->state_mutex_);

    for (auto* module : storage_modules)
    {
        auto* storage = static_cast<StorageModule*>(module);
        while (fuel_ < capacity_ && storage->ResourceCount(RESOURCE_TYPE::ROCKET_FUEL) > 0)
        {
            storage->TakeFromInventory({{RESOURCE_TYPE::ROCKET_FUEL, 1}});
            fuel_ = std::min(capacity_, fuel_ + fuel_per_object_);
        }
        if (fuel_ >= capacity_)
            break;
    }
}

boost::json::object FuelTankModule::Handle(boost::json::object request)
{
    const auto method = boost::json::value_to<std::string>(request.at("method"));
    if (method == "get_max_fuel" || method == "get_fuel")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            if (method == "get_max_fuel")
                result["max_fuel"] = capacity_;
            else
                result["fuel"] = fuel_;
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    return boost::json::object{};
}
