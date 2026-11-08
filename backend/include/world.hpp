#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Vector2
{
public:
    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float x, float y) : x(x), y(y) {}
    float x;
    float y;
    float Magnitude() const
    {
        return std::sqrt(x * x + y * y);
    }
};

enum ENTITY_TYPE
{
    ASTEROID,
    SHIP,
    STATION
};

class Entity
{
    Vector2 position;
    Vector2 velocity;
    ENTITY_TYPE type_;

    std::size_t id_; // Unique identifier for the entity


public:
    Entity(ENTITY_TYPE type, std::size_t id) : position{0.0f, 0.0f}, velocity{0.0f, 0.0f}, type_(type), id_(id){}
    Entity(const Vector2& pos, const Vector2& vel, ENTITY_TYPE type, std::size_t id) : position(pos), velocity(vel), type_(type), id_(id) {}

    Vector2 GetPosition() const { return position; }
    std::size_t GetId() const { return id_; }

    void SetPosition(const Vector2& pos) { position = pos; }

    Vector2 GetVelocity() const { return velocity; }

    void SetVelocity(const Vector2& vel) { velocity = vel; }

    ENTITY_TYPE GetType() const { return type_; }

    virtual ~Entity() = default;

};

class Asteroid : public Entity
{
    std::vector<std::pair<std::string, float>> resources;
public:

    Asteroid(std::size_t id) : Entity(ASTEROID, id) {}
    Asteroid(const Vector2& pos, const Vector2& vel, std::size_t id) : Entity(pos, vel, ASTEROID, id) {}

    void AddResource(const std::string& name, float amount) {
        resources.emplace_back(name, amount);
    }

    std::vector<std::pair<std::string, float>> MineResource(float amount);

    const std::vector<std::pair<std::string, float>>& GetResources() const {
        return resources;
    }
};


class World
{
    std::unordered_map<std::size_t, Entity*> entities;
    std::unordered_map<std::size_t, Entity*> asteroids;

    Vector2 size{1000.0f, 1000.0f};

    std::size_t next_entity_id = 1;

public:
    std::unordered_map<std::size_t, Entity*> GetEntities() const { return entities; }
    World();
    ~World()
    {
        for (const auto& pair : entities) {
            delete pair.second;
        }
    }
};

World* LoadWorld(const std::string& filename);

void UpdateWorld(World* world);
