#include <iostream>
#include <fstream>
#include <random>


#include "world.hpp"

World* LoadWorld(const std::string& filename = "world.json")
{
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cout << "Не удалось открыть файл!" << std::endl;
        return new World(); 
    }

    World* world = new World();

    file.close();
    return world;
}

void UpdateWorld(World* world)
{
    
}

std::vector<std::pair<std::string, float>> Asteroid::MineResource(float amount) {
    std::vector<std::pair<std::string, float>> minedResources;
    if (amount <= 0.0f) {
        return minedResources;
    }

    for (auto& resource : resources) {
        if (resource.second <= 0.0f) {
            continue;
        }

        const float minedAmount = std::min(amount, resource.second);
        minedResources.emplace_back(resource.first, minedAmount);
        resource.second -= minedAmount;
    }

    if (std::all_of(resources.begin(), resources.end(), [](const auto& resource) {
        return resource.second <= 0.0f;
    })) {
       //TODO
    }

    return minedResources;
}


World::World()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-size.x / 2.0f, size.x / 2.0f);
    for(int i = 0; i < 100; ++i)
    {
        float x = dist(gen);
        float y = dist(gen);

        Asteroid* asteroid = new Asteroid(Vector2(x, y), Vector2(0.0f, 0.0f), next_entity_id++);
        asteroid->AddResource("Iron", 100.0f);
        asteroid->AddResource("Gold", 50.0f);
        entities.insert({asteroid->GetId(), asteroid});
        asteroids.insert({asteroid->GetId(), asteroid});
    }
}