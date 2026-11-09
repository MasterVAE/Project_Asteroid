#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "resources.hpp"

float StorageModule::GetResourceAmount(RESOURCE_TYPE resource) const
{
    auto it = inventory.find(resource);
    if (it != inventory.end()) {
        return it->second;
    }
    return 0.0f;
};
void StorageModule::AddResource(RESOURCE_TYPE resource, float amount)
{
    inventory[resource] += amount;
}
void StorageModule::RemoveResource(RESOURCE_TYPE resource, float amount)
{
    auto it = inventory.find(resource);
    if (it != inventory.end()) {
        it->second -= amount;
        if (it->second <= 0.0f) {
            inventory.erase(it);
        }
    }
}

void StorageModule::Update(float delta_time) {}


void MiningModule::Update(float delta_time) 
{
    if(target_asteroid && storage_module)
    {
        // Example mining logic: mine 1 unit of resource per second
        float mining_rate = 1.0f; // units per second
        float amount_mined = mining_rate * delta_time;

        // Assuming the asteroid has a method to get and remove resources
        for (const auto& resource : target_asteroid->GetInventory()) {
            if (resource.second > 0) {
                float mined_amount = std::min(amount_mined, resource.second);
                storage_module->AddResource(resource.first, mined_amount);
                target_asteroid->TakeResource(resource.first, mined_amount);
                break; // Mine one type of resource at a time
            }
        }
    }
}

void PropulsionModule::Update(float delta_time) 
{
    // Example propulsion logic: move towards the target position
    Vector2 current_position = GetParentCraft()->GetPosition(); // Assuming this method exists
    Vector2 direction = target - current_position;
    float distance = std::sqrt(direction.x * direction.x + direction.y * direction.y);

    if (distance > 5.0f) {
        direction.x /= distance;
        direction.y /= distance;

        // Update velocity based on acceleration and delta_time
        Vector2 new_velocity = GetParentCraft()->GetVelocity(); // Assuming this method exists
        new_velocity.x += direction.x * acceleration * delta_time;
        new_velocity.y += direction.y * acceleration * delta_time;

        GetParentCraft()->SetVelocity(new_velocity); // Assuming this method exists
    }
    else
    {
        FlyTo(GetRandomPosition(Vector2(1000.0f, 1000.0f))); // Fly to a new random position when close enoughß
    }
}
