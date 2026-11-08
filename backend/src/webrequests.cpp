#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

#include "webserver.hpp"
#include "world.hpp"

namespace json = boost::json;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

json::value GetMap(const World* world)
{
    json::object world_state;

    json::array entities_array;
    for (const auto& entity : world->GetEntities()) {
        json::object entity_obj;


        // FIXME: Determine the type of entity and set the appropriate fields
        entity_obj["type"] = "asteroid"; // Assuming all entities are asteroids for now
        entity_obj["position"] = { {"x", entity.second->GetPosition().x}, {"y", entity.second->GetPosition().y} };
        entity_obj["velocity"] = { {"x", entity.second->GetVelocity().x}, {"y", entity.second->GetVelocity().y} };

        entities_array.push_back(entity_obj);
    }

    world_state["entities"] = entities_array;

    return world_state;
}


json::value GetEntity(const Entity* entity)
{
    if(entity->GetType() == ASTEROID)
    {
        const Asteroid* asteroid = dynamic_cast<const Asteroid*>(entity);
        if(asteroid)
        {
            json::object entity_obj;
            entity_obj["type"] = "asteroid";
            entity_obj["position"] = { {"x", asteroid->GetPosition().x}, {"y", asteroid->GetPosition().y} };
            entity_obj["velocity"] = { {"x", asteroid->GetVelocity().x}, {"y", asteroid->GetVelocity().y} };

            json::array resources_array;
            for (const auto& resource : asteroid->GetResources()) {
                json::object resource_obj;
                resource_obj["name"] = resource.first;
                resource_obj["amount"] = resource.second;
                resources_array.push_back(resource_obj);
            }
            entity_obj["resources"] = resources_array;

            return entity_obj;
        }
    }

    return json::object(); // Return an empty object if the entity is not an asteroid
}