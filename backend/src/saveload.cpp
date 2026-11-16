#include "game.hpp"

#include <boost/filesystem/operations.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
namespace json = boost::json;

constexpr unsigned int save_version = 1;

const std::string& ResourceName(RESOURCE_TYPE type)
{
    for (const auto& resource : RESOURCES)
        if (resource.type == type) return resource.name;
    throw std::runtime_error("Unknown resource type in world");
}

RESOURCE_TYPE ResourceType(const json::value& value)
{
    const auto name = json::value_to<std::string>(value);
    for (const auto& resource : RESOURCES)
        if (resource.name == name) return resource.type;
    throw std::runtime_error("Unknown saved resource: " + name);
}

std::string ModuleName(MODULE_TYPE type)
{
    if (type == MODULE_TYPE::NONE) return "none";
    for (const auto& module : MODULES_DATA)
        if (module.type == type) return module.name;
    throw std::runtime_error("Unknown module type in world");
}

MODULE_TYPE ModuleType(const json::value& value)
{
    const auto name = json::value_to<std::string>(value);
    if (name == "none") return MODULE_TYPE::NONE;
    for (const auto& module : MODULES_DATA)
        if (module.name == name) return module.type;
    throw std::runtime_error("Unknown saved module: " + name);
}

std::size_t Count(const json::value& value)
{
    if ((!value.is_int64() && !value.is_uint64()) ||
        (value.is_int64() && value.as_int64() < 0))
        throw std::runtime_error("Expected a non-negative integer in save file");
    return json::value_to<std::size_t>(value);
}

double Number(float value)
{
    if (!std::isfinite(value))
        throw std::runtime_error("Cannot save a non-finite world value");
    return value;
}

float Number(const json::value& value)
{
    if (!value.is_number())
        throw std::runtime_error("Expected a number in save file");
    const double number = json::value_to<double>(value);
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
        throw std::runtime_error("Saved number is outside the finite float range");
    return static_cast<float>(number);
}

float NonNegative(const json::object& object, const char* key)
{
    const float value = Number(object.at(key));
    if (value < 0) throw std::runtime_error(std::string("Negative saved value: ") + key);
    return value;
}

float Positive(const json::object& object, const char* key)
{
    const float value = Number(object.at(key));
    if (value <= 0) throw std::runtime_error(std::string("Expected a positive saved value: ") + key);
    return value;
}

void CheckRange(float value, float minimum, float maximum, const char* name)
{
    if (value < minimum || value > maximum)
        throw std::runtime_error(std::string("Saved value is out of range: ") + name);
}

void CheckProgress(float time, float cooldown)
{
    if (time < 0 || time >= cooldown)
        throw std::runtime_error("Saved production progress is out of range");
}

json::array SaveVector(Vector2 vector)
{
    return {Number(vector.x), Number(vector.y)};
}

Vector2 LoadVector(const json::value& value)
{
    const auto& array = value.as_array();
    if (array.size() != 2) throw std::runtime_error("Saved vector must contain two numbers");
    return {Number(array[0]), Number(array[1])};
}

json::array SaveResources(const std::unordered_map<RESOURCE_TYPE, std::size_t>& resources)
{
    std::vector<RESOURCE_TYPE> types;
    types.reserve(resources.size());
    for (const auto& entry : resources) types.push_back(entry.first);
    std::sort(types.begin(), types.end());
    json::array result;
    for (const auto type : types)
        result.push_back(json::object{{"resource", ResourceName(type)}, {"count", resources.at(type)}});
    return result;
}

std::unordered_map<RESOURCE_TYPE, std::size_t> LoadResources(const json::value& value)
{
    std::unordered_map<RESOURCE_TYPE, std::size_t> result;
    for (const auto& entry : value.as_array()) {
        const auto& resource = entry.as_object();
        if (!result.emplace(ResourceType(resource.at("resource")), Count(resource.at("count"))).second)
            throw std::runtime_error("Duplicate resource in save file");
    }
    return result;
}

void CheckInventory(const std::unordered_map<RESOURCE_TYPE, std::size_t>& inventory, std::size_t capacity)
{
    for (const auto& entry : inventory) {
        if (entry.second > capacity) throw std::runtime_error("Saved storage capacity exceeded");
        capacity -= entry.second;
    }
}

void CheckAsteroid(const std::unordered_map<RESOURCE_TYPE, std::size_t>& resources)
{
    std::size_t total = 0;
    for (const auto& entry : resources) {
        if (entry.second > std::numeric_limits<std::size_t>::max() - total)
            throw std::runtime_error("Saved asteroid resource count overflow");
        total += entry.second;
    }
    if (total == 0) throw std::runtime_error("Saved asteroid must contain resources");
}

json::object SaveRecipe(const RECIPE& recipe)
{
    json::array input;
    json::array output;
    for (const auto resource : recipe.input) input.push_back(json::value(ResourceName(resource)));
    for (const auto resource : recipe.output) output.push_back(json::value(ResourceName(resource)));
    return {{"input", input}, {"output", output}};
}

RECIPE LoadRecipe(const json::value& value)
{
    const auto& object = value.as_object();
    RECIPE recipe;
    for (const auto& resource : object.at("input").as_array()) recipe.input.push_back(ResourceType(resource));
    for (const auto& resource : object.at("output").as_array()) recipe.output.push_back(ResourceType(resource));
    return recipe;
}

std::filesystem::path FilePath(const std::string& filename)
{
    if (filename.empty() || filename.find('\0') != std::string::npos)
        throw std::runtime_error("Save filename must not be empty or contain NUL");
    return std::filesystem::u8path(filename);
}

void WriteSave(const std::filesystem::path& target, const json::object& document)
{
    // Serialize first, then replace the destination only after the complete file is closed.
    const auto data = json::serialize(document) + '\n';
    auto temporary = target;
    temporary += boost::filesystem::unique_path(".%%%%-%%%%-%%%%.tmp").string();
    try {
        std::ofstream stream;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(temporary, std::ios::binary | std::ios::trunc);
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.close();
        std::filesystem::rename(temporary, target);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
} // namespace

void World::Save(std::string filename)
{
    try {
        const auto path = FilePath(filename);
        if (updating_) throw std::runtime_error("Save is only available between world updates");

        std::vector<Entity*> entities;
        for (const auto& entry : entities_)
            if (pending_destroy_.count(entry.first) == 0) entities.push_back(entry.second.get());
        for (const auto& entity : pending_spawn_) entities.push_back(entity.get());
        std::sort(entities.begin(), entities.end(), [](const Entity* left, const Entity* right) {
            return left->GetId() < right->GetId();
        });

        // Freeze all craft inventories together so a concurrent transfer cannot
        // appear in both crafts (or neither craft) in the same snapshot.
        std::vector<std::unique_lock<std::mutex>> state_locks;
        state_locks.reserve(entities.size());
        for (auto* entity : entities)
            if (entity->GetType() == ENTITY_TYPE::CRAFT)
                state_locks.emplace_back(static_cast<Craft*>(entity)->state_mutex_);

        json::array saved_entities;
        for (auto* entity : entities) {
            Craft* craft = entity->GetType() == ENTITY_TYPE::CRAFT ? static_cast<Craft*>(entity) : nullptr;
            json::object saved{
                {"id", entity->GetId()},
                {"type", craft ? "craft" : "asteroid"},
                {"name", entity->GetName()},
                {"position", SaveVector(entity->GetPosition())},
                {"velocity", SaveVector(entity->GetVelocity())},
                {"rotation", Number(entity->GetRotation())},
                {"angular_velocity", Number(entity->GetAngularVelocity())}
            };
            if (!craft) {
                const auto& resources = static_cast<Asteroid*>(entity)->GetResources();
                CheckAsteroid(resources);
                saved["resources"] = SaveResources(resources);
                saved_entities.push_back(std::move(saved));
                continue;
            }

            {
                std::lock_guard send_lock(craft->send_mutex_);
                saved["script"] = craft->script_path_;
            }
            saved["script_data"] = craft->script_data;
            json::object indices;
            for (const auto& entry : craft->module_indices_) indices[ModuleName(entry.first)] = entry.second;
            saved["module_indices"] = std::move(indices);
            json::array modules;
            for (const auto& module : craft->modules_) {
                json::object data{{"type", ModuleName(module->GetType())}, {"name", module->GetName()}};
                switch (module->GetType()) {
                case MODULE_TYPE::PROPULSION: {
                    const auto* current = static_cast<PropulsionModule*>(module.get());
                    data["max_thrust"] = Number(current->max_thrust_);
                    data["throttle"] = Number(current->throttle_);
                    data["fuel_consumption_per_second"] = Number(current->fuel_consumption_per_second_);
                    break;
                }
                case MODULE_TYPE::STORAGE: {
                    const auto* current = static_cast<StorageModule*>(module.get());
                    CheckInventory(current->inventory_, current->capacity_);
                    data["capacity"] = current->capacity_;
                    data["inventory"] = SaveResources(current->inventory_);
                    data["ready_to_recieve"] = current->ready_to_recieve_;
                    break;
                }
                case MODULE_TYPE::RCS: {
                    const auto* current = static_cast<RCSModule*>(module.get());
                    data["max_gyro"] = Number(current->max_gyro_);
                    data["gyro"] = Number(current->gyro_);
                    data["max_thrust"] = Number(current->max_thrust_);
                    data["thrust"] = SaveVector(current->thrust_);
                    break;
                }
                case MODULE_TYPE::DRILL: {
                    const auto* current = static_cast<DrillModule*>(module.get());
                    data["drill_cooldown"] = Number(current->drill_cooldown_);
                    data["timer"] = Number(current->timer_);
                    data["enabled"] = current->enabled_;
                    data["power_consumption"] = Number(current->power_consuption_);
                    break;
                }
                case MODULE_TYPE::SCANNER: {
                    const auto* current = static_cast<ScanModule*>(module.get());
                    data["radius"] = Number(current->radius);
                    data["scan_power"] = Number(current->scan_power_);
                    break;
                }
                case MODULE_TYPE::POWER: {
                    const auto* current = static_cast<PowerModule*>(module.get());
                    data["capacity"] = Number(current->capacity_);
                    data["charge"] = Number(current->charge_);
                    data["generation_per_second"] = Number(current->generation_per_second_);
                    break;
                }
                case MODULE_TYPE::FACTORY: {
                    const auto* current = static_cast<FactoryModel*>(module.get());
                    data["production_cooldown"] = Number(current->production_cooldown_);
                    data["energy_consumption_per_second"] = Number(current->energy_consuption_per_second_);
                    data["time"] = Number(current->time_);
                    json::array queue;
                    for (const auto& recipe : current->queue_) queue.push_back(SaveRecipe(recipe));
                    data["queue"] = std::move(queue);
                    break;
                }
                case MODULE_TYPE::SHIPYARD: {
                    const auto* current = static_cast<ShipyardModule*>(module.get());
                    data["production_cooldown"] = Number(current->production_cooldown_);
                    data["energy_consumption_per_second"] = Number(current->energy_consuption_per_second_);
                    data["building_radius"] = Number(current->building_radius_);
                    data["time"] = Number(current->time_);
                    json::array queue;
                    for (const auto& order : current->queue_)
                        queue.push_back(json::object{{"craft_id", order.craft_id}, {"type", ModuleName(order.type)}});
                    data["queue"] = std::move(queue);
                    break;
                }
                case MODULE_TYPE::FUEL_TANK: {
                    const auto* current = static_cast<FuelTankModule*>(module.get());
                    CheckRange(current->fuel_, 0, current->capacity_, "fuel");
                    data["capacity"] = Number(current->capacity_);
                    data["fuel"] = Number(current->fuel_);
                    data["fuel_per_object"] = Number(current->fuel_per_object_);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported module in world");
                }
                modules.push_back(std::move(data));
            }
            saved["modules"] = std::move(modules);
            saved_entities.push_back(std::move(saved));
        }
        state_locks.clear();
        WriteSave(path, {{"format", "project_asteroid"}, {"version", save_version},
                         {"next_id", next_id_}, {"entities", std::move(saved_entities)}});
    } catch (const std::exception& error) {
        std::cerr << "Save failed (" << filename << "): " << error.what() << '\n';
    }
}

void World::Load(std::string filename)
{
    try {
        if (updating_) throw std::runtime_error("Load is only available between world updates");
        std::ifstream stream(FilePath(filename), std::ios::binary);
        stream.exceptions(std::ios::badbit);
        if (!stream.is_open()) throw std::runtime_error("Cannot open save file");
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const auto document = json::parse(text);
        const auto& root = document.as_object();
        if (json::value_to<std::string>(root.at("format")) != "project_asteroid" ||
            Count(root.at("version")) != save_version)
            throw std::runtime_error("Unsupported save format or version");
        const auto next_id = Count(root.at("next_id"));
        if (next_id == std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("Saved entity ID counter would overflow");

        // Build and validate everything before stopping scripts or replacing the current world.
        std::unordered_map<std::size_t, std::unique_ptr<Entity>> restored;
        std::vector<std::pair<Craft*, std::string>> scripts;
        for (const auto& entry : root.at("entities").as_array()) {
            const auto& saved = entry.as_object();
            const auto id = Count(saved.at("id"));
            if (id >= next_id || restored.count(id))
                throw std::runtime_error("Duplicate entity ID or invalid next_id in save file");
            const auto name = json::value_to<std::string>(saved.at("name"));
            if (name.find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::runtime_error("Saved entity name must not be empty");
            const auto position = LoadVector(saved.at("position"));
            const auto type = json::value_to<std::string>(saved.at("type"));
            std::unique_ptr<Entity> entity;
            if (type == "asteroid") {
                auto asteroid = std::make_unique<Asteroid>(position, name);
                asteroid->GetResources() = LoadResources(saved.at("resources"));
                CheckAsteroid(asteroid->GetResources());
                entity = std::move(asteroid);
            }
            else if (type == "craft") {
                auto craft = std::make_unique<Craft>(position, name);
                if (const auto* data = saved.if_contains("script_data"))
                    craft->script_data = json::value_to<std::string>(*data);
                for (const auto& module_entry : saved.at("modules").as_array()) {
                    const auto& data = module_entry.as_object();
                    const auto module_type = ModuleType(data.at("type"));
                    auto& module = craft->AddModule(module_type);
                    module.SetName(json::value_to<std::string>(data.at("name")));
                    switch (module_type) {
                    case MODULE_TYPE::PROPULSION: {
                        auto& current = static_cast<PropulsionModule&>(module);
                        current.max_thrust_ = NonNegative(data, "max_thrust");
                        current.throttle_ = Number(data.at("throttle"));
                        CheckRange(current.throttle_, 0, 1, "throttle");
                        current.fuel_consumption_per_second_ = NonNegative(data, "fuel_consumption_per_second");
                        break;
                    }
                    case MODULE_TYPE::STORAGE: {
                        auto& current = static_cast<StorageModule&>(module);
                        current.capacity_ = Count(data.at("capacity"));
                        current.inventory_ = LoadResources(data.at("inventory"));
                        CheckInventory(current.inventory_, current.capacity_);
                        current.ready_to_recieve_ = data.at("ready_to_recieve").as_bool();
                        break;
                    }
                    case MODULE_TYPE::RCS: {
                        auto& current = static_cast<RCSModule&>(module);
                        current.max_gyro_ = NonNegative(data, "max_gyro");
                        current.gyro_ = Number(data.at("gyro"));
                        CheckRange(current.gyro_, -1, 1, "gyro");
                        current.max_thrust_ = NonNegative(data, "max_thrust");
                        current.thrust_ = LoadVector(data.at("thrust"));
                        if (current.thrust_.LengthSquared() > 1.0f + 8 * std::numeric_limits<float>::epsilon())
                            throw std::runtime_error("Saved RCS thrust exceeds one");
                        break;
                    }
                    case MODULE_TYPE::DRILL: {
                        auto& current = static_cast<DrillModule&>(module);
                        current.drill_cooldown_ = Positive(data, "drill_cooldown");
                        current.timer_ = Number(data.at("timer"));
                        CheckProgress(current.timer_, current.drill_cooldown_);
                        current.enabled_ = data.at("enabled").as_bool();
                        current.power_consuption_ = NonNegative(data, "power_consumption");
                        break;
                    }
                    case MODULE_TYPE::SCANNER: {
                        auto& current = static_cast<ScanModule&>(module);
                        current.radius = NonNegative(data, "radius");
                        current.scan_power_ = NonNegative(data, "scan_power");
                        break;
                    }
                    case MODULE_TYPE::POWER: {
                        auto& current = static_cast<PowerModule&>(module);
                        current.capacity_ = NonNegative(data, "capacity");
                        current.charge_ = NonNegative(data, "charge");
                        CheckRange(current.charge_, 0, current.capacity_, "charge");
                        current.generation_per_second_ = NonNegative(data, "generation_per_second");
                        break;
                    }
                    case MODULE_TYPE::FACTORY: {
                        auto& current = static_cast<FactoryModel&>(module);
                        current.production_cooldown_ = Positive(data, "production_cooldown");
                        current.energy_consuption_per_second_ = NonNegative(data, "energy_consumption_per_second");
                        current.time_ = Number(data.at("time"));
                        CheckProgress(current.time_, current.production_cooldown_);
                        for (const auto& recipe : data.at("queue").as_array()) current.queue_.push_back(LoadRecipe(recipe));
                        if (current.queue_.empty() && current.time_ != 0)
                            throw std::runtime_error("Saved factory progress has no order");
                        break;
                    }
                    case MODULE_TYPE::SHIPYARD: {
                        auto& current = static_cast<ShipyardModule&>(module);
                        current.production_cooldown_ = Positive(data, "production_cooldown");
                        current.energy_consuption_per_second_ = NonNegative(data, "energy_consumption_per_second");
                        current.building_radius_ = NonNegative(data, "building_radius");
                        current.time_ = Number(data.at("time"));
                        CheckProgress(current.time_, current.production_cooldown_);
                        for (const auto& queue_entry : data.at("queue").as_array()) {
                            const auto& order = queue_entry.as_object();
                            current.queue_.push_back({Count(order.at("craft_id")), ModuleType(order.at("type"))});
                        }
                        if (current.queue_.empty() && current.time_ != 0)
                            throw std::runtime_error("Saved shipyard progress has no order");
                        break;
                    }
                    case MODULE_TYPE::FUEL_TANK: {
                        auto& current = static_cast<FuelTankModule&>(module);
                        current.capacity_ = NonNegative(data, "capacity");
                        current.fuel_ = NonNegative(data, "fuel");
                        CheckRange(current.fuel_, 0, current.capacity_, "fuel");
                        current.fuel_per_object_ = Positive(data, "fuel_per_object");
                        break;
                    }
                    default:
                        throw std::runtime_error("Unsupported saved module");
                    }
                }
                std::unordered_map<MODULE_TYPE, std::size_t> indices;
                for (const auto& item : saved.at("module_indices").as_object()) {
                    const auto module_type = ModuleType(json::value(item.key()));
                    const auto count = Count(item.value());
                    const auto existing = craft->module_indices_.find(module_type);
                    if (module_type == MODULE_TYPE::NONE || count == std::numeric_limits<std::size_t>::max() ||
                        (existing != craft->module_indices_.end() && count < existing->second))
                        throw std::runtime_error("Invalid saved module index");
                    indices.emplace(module_type, count);
                }
                for (const auto& item : craft->module_indices_)
                    if (!indices.count(item.first)) throw std::runtime_error("Missing saved module index");
                craft->module_indices_ = std::move(indices);
                craft->RecalculateMass();
                const auto script = json::value_to<std::string>(saved.at("script"));
                if (script.find('\0') != std::string::npos)
                    throw std::runtime_error("Saved script path contains NUL");
                if (!script.empty()) scripts.emplace_back(craft.get(), script);
                entity = std::move(craft);
            }
            else {
                throw std::runtime_error("Unknown saved entity type: " + type);
            }
            entity->SetId(id);
            entity->SetVelocity(LoadVector(saved.at("velocity")));
            entity->SetRotation(Number(saved.at("rotation")));
            entity->SetAngularVelocity(Number(saved.at("angular_velocity")));
            entity->world = this;
            restored.emplace(id, std::move(entity));
        }

        // Every old script must finish while its old world and its targets still exist.
        for (const auto& entry : entities_)
            if (entry.second->GetType() == ENTITY_TYPE::CRAFT) static_cast<Craft*>(entry.second.get())->StopScript();
        for (const auto& entity : pending_spawn_)
            if (entity->GetType() == ENTITY_TYPE::CRAFT) static_cast<Craft*>(entity.get())->StopScript();
        entities_.swap(restored);
        pending_spawn_.clear();
        pending_destroy_.clear();
        next_id_ = next_id;
        updating_ = false;
        restored.clear();

        // Scripts may immediately query other entities, so launch only after the entire world is installed.
        for (const auto& script : scripts) script.first->LaunchScript(script.second);
    } catch (const std::exception& error) {
        std::cerr << "Load failed (" << filename << "): " << error.what() << '\n';
    }
}
