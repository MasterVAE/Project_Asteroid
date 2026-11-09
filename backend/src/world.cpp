#include "world.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "principal.hpp"
#include "resources.hpp"

Vector2 GetRandomPosition(const Vector2& size)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-size.x / 2.0f, size.x / 2.0f);
    return Vector2(dist(gen), dist(gen));
}

World* LoadWorld(const std::string& filename = "world.json")
{
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cout << "Не удалось открыть файл!" << std::endl;

        // TODO загрузка мира

        return new World(); 
    }

    World* world = new World();

    file.close();
    return world;
}

void UpdateWorld(World* world, float delta_time)
{
    for (const auto& pair : world->GetEntities()) {
        Entity* entity = pair.second;

        entity->Update(delta_time); // Вызов метода Update для каждой сущности

        Vector2 pos = entity->GetPosition();
        Vector2 vel = entity->GetVelocity();

        pos.x += vel.x * delta_time;
        pos.y += vel.y * delta_time;

        entity->SetPosition(pos);
    }
}

void SaveWorld(const World* world, const std::string& filename = "world.json")
{
    std::ofstream file(filename);

    if (!file.is_open()) {
        std::cout << "Не удалось открыть файл для записи!" << std::endl;
        return;
    }
    // TODO: Implement saving the world state to the file in JSON format
    
    file.close();
}


World::World()
{
    for(int i = 0; i < 100; ++i)
    {
        Vector2 pos = GetRandomPosition(size);
        Asteroid* asteroid = new Asteroid(pos, Vector2(0.0f, 0.0f), next_entity_id++);
        asteroid->AddResource(RESOURCE_TYPE::IRON, 100.0f);
        asteroid->AddResource(RESOURCE_TYPE::COPPER, 50.0f);
        entities.insert({asteroid->GetId(), asteroid});
        asteroids.insert({asteroid->GetId(), asteroid});
    }
    for (int i = 0; i < 10; ++i)
    {
        CreateEntity();
    }
}

std::size_t World::CreateEntity(const Principal* owner)
{
    if (!owner || principals_.Find(owner->user_id) != owner)
        throw std::invalid_argument("Entity owner must belong to this world's principal registry");
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id = next_entity_id;
    auto craft = std::make_unique<Craft>(GetRandomPosition(size), Vector2{}, id);
    craft->owner = owner;
    craft->AddModule(std::make_unique<MiningModule>(craft.get()));
    craft->AddModule(std::make_unique<StorageModule>(craft.get()));
    craft->AddModule(std::make_unique<PropulsionModule>(craft.get(), 1.0f));
    entities.emplace(id, craft.get());
    try { crafts.emplace(id, craft.get()); }
    catch (...) { entities.erase(id); throw; }
    ++next_entity_id;
    craft.release();
    return id;
}
