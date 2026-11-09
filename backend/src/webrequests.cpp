#include "webserver.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "world.hpp"
#include "resources.hpp"

namespace json = boost::json;

json::value GetMap(const World* world)
{
    json::object world_state;

    json::array entities_array;
    for (const auto& entity : world->GetEntities()) {
        json::object entity_obj;
        entity_obj["id"] = entity.first;

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
            entity_obj["type"] = "craft";
            entity_obj["position"] = { {"x", craft->GetPosition().x}, {"y", craft->GetPosition().y} };
            entity_obj["velocity"] = { {"x", craft->GetVelocity().x}, {"y", craft->GetVelocity().y} };

            json::array modules_array;
            for (const auto& module : craft->GetModules()) {
                json::object module_obj;
                module_obj["type"] = module->GetType();
                modules_array.push_back(module_obj);
            }
            entity_obj["modules"] = modules_array;

            return entity_obj;
        }
    }

    return json::object(); // Return an empty object if the entity is not an asteroid or craft
}
