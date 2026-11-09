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
    if (!has_target || delta_time <= 0.0f || acceleration <= 0.0f) return;
    auto* craft = GetParentCraft();
    const Vector2 offset = target - craft->GetPosition();
    const float distance = std::hypot(offset.x, offset.y);
    if (distance <= 0.1f) {
        craft->SetPosition(target);
        craft->SetVelocity(Vector2{});
        has_target = false;
        if (craft->owner == Principal::Server()) FlyTo(GetRandomPosition(Vector2(1000.0f, 1000.0f)));
        return;
    }

    // Limit acceleration and brake as the remaining distance decreases.
    const float speed = static_cast<float>(std::min(
        std::sqrt(2.0 * acceleration * distance), static_cast<double>(distance) / delta_time));
    const Vector2 desired = (offset / distance) * speed;
    const Vector2 velocity = craft->GetVelocity();
    Vector2 change = desired - velocity;
    const float change_length = std::hypot(change.x, change.y);
    const float max_change = acceleration * delta_time;
    if (change_length > max_change) change *= max_change / change_length;
    craft->SetVelocity(velocity + change);
}
