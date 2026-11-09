#include "webserver.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "world.hpp"
#include "resources.hpp"
#include "principal.hpp"

namespace json = boost::json;

json::value GetMap(const World* world)
{
    json::object world_state;

    json::array entities_array;
    for (const auto& entity : world->GetEntities()) {
        json::object entity_obj;
        entity_obj["id"] = entity.first;
        entity_obj["owner_id"] = entity.second->owner->user_id;

        if(entity.second->GetType() == ASTEROID)
        {
            entity_obj["type"] = "asteroid";
        }
        else if(entity.second->GetType() == CRAFT)
        {
            entity_obj["type"] = "craft";
        }
        else
        {
            entity_obj["type"] = "unknown";
        }
        entity_obj["position"] = { {"x", entity.second->GetPosition().x}, {"y", entity.second->GetPosition().y} };
        entity_obj["velocity"] = { {"x", entity.second->GetVelocity().x}, {"y", entity.second->GetVelocity().y} };

        entities_array.push_back(entity_obj);
    }

    world_state["entities"] = entities_array;

    return world_state;
}


json::value GetEntity(const Entity* entity)
{
    if(entity->GetType() == ENTITY_TYPE::ASTEROID)
    {
        const Asteroid* asteroid = dynamic_cast<const Asteroid*>(entity);
        if(asteroid)
        {
            json::object entity_obj;
            entity_obj["id"] = asteroid->GetId();
            entity_obj["owner_id"] = asteroid->owner->user_id;
            entity_obj["type"] = "asteroid";
            entity_obj["position"] = { {"x", asteroid->GetPosition().x}, {"y", asteroid->GetPosition().y} };
            entity_obj["velocity"] = { {"x", asteroid->GetVelocity().x}, {"y", asteroid->GetVelocity().y} };

            json::array resources_array;
            for (const auto& resource : asteroid->GetInventory()) {
                json::object resource_obj;
                resource_obj["name"] = RESOURCE_DATA.at(resource.first).name;
                resource_obj["amount"] = resource.second;
                resources_array.push_back(resource_obj);
            }
            entity_obj["resources"] = resources_array;

            return entity_obj;
        }
    }
    else if (entity->GetType() == ENTITY_TYPE::CRAFT)
    {
        const Craft* craft = dynamic_cast<const Craft*>(entity);
        if(craft)
        {
            json::object entity_obj;
            entity_obj["id"] = craft->GetId();
            entity_obj["owner_id"] = craft->owner->user_id;
            entity_obj["type"] = "craft";
            entity_obj["position"] = { {"x", craft->GetPosition().x}, {"y", craft->GetPosition().y} };
            entity_obj["velocity"] = { {"x", craft->GetVelocity().x}, {"y", craft->GetVelocity().y} };

            json::array modules_array;
            for (const auto& module : craft->GetModules()) {
                json::object module_obj;
                module_obj["type"] = module->GetType();
                if (module->GetType() == PROPULSION) {
                    const auto& target = static_cast<const PropulsionModule*>(module.get())->GetTarget();
                    module_obj["target"] = {{"x", target.x}, {"y", target.y}};
                }
                modules_array.push_back(module_obj);
            }
            entity_obj["modules"] = modules_array;

            return entity_obj;
        }
    }

    return json::object(); // Return an empty object if the entity is not an asteroid or craft
}

json::value GetMyEntities(const World* world, const Principal* owner)
{
    json::array entities;
    for (const auto& entry : world->GetEntities()) {
        if (entry.second->owner == owner) entities.push_back(GetEntity(entry.second));
    }
    return json::object{{"entities", std::move(entities)}};
}

json::value CreateEntity(World* world, const Principal* owner)
{
    const auto id = world->CreateEntity(owner);
    std::lock_guard<std::mutex> lock(world->GetMutex());
    return GetEntity(world->GetEntities().at(id));
}
