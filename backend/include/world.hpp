#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "resources.hpp"

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
    Vector2 operator+(const Vector2& other) const
    {
        return Vector2(x + other.x, y + other.y);
    }
    Vector2 operator-(const Vector2& other) const
    {
        return Vector2(x - other.x, y - other.y);
    }
    Vector2 operator*(float scalar) const
    {
        return Vector2(x * scalar, y * scalar);
    }
    Vector2 operator/(float scalar) const
    {
        return Vector2(x / scalar, y / scalar);
    }
    Vector2& operator+=(const Vector2& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }
    Vector2& operator-=(const Vector2& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }
    Vector2& operator*=(float scalar)
    {           
        x *= scalar;
        y *= scalar;
        return *this;
    }
    Vector2& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }
};

enum ENTITY_TYPE
{
    ASTEROID,
    CRAFT
};

class Entity
{
    Vector2 position;
    Vector2 velocity;
    ENTITY_TYPE type_;

    std::size_t id_;

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

    virtual void Update(float delta_time) = 0;
};

class Asteroid : public Entity
{
    std::unordered_map<RESOURCE_TYPE, float> inventory;
public:

    Asteroid(std::size_t id) : Entity(ASTEROID, id) {}
    Asteroid(const Vector2& pos, const Vector2& vel, std::size_t id) : Entity(pos, vel, ASTEROID, id) {}

    void AddResource(RESOURCE_TYPE resource, float amount)
    {
        inventory[resource] += amount;
    }
    void TakeResource(RESOURCE_TYPE resource, float amount)
    {
        auto it = inventory.find(resource);
        if (it != inventory.end()) {
            it->second -= amount;
            if (it->second <= 0.0f) {
                inventory.erase(it);
            }
        }
    }
    const std::unordered_map<RESOURCE_TYPE, float>& GetInventory() const { return inventory; }



    void Update(float delta_time) override { }
};

enum MODULE_TYPE
{
    MINING,
    CRAFTING,
    STORAGE,
    REACTOR,
    PROPULSION
};
class Craft;

class Module
{
    MODULE_TYPE type_;
    Craft* parent_craft_; // Указатель на родительский корабль, если необходимо

public:
    Module(MODULE_TYPE type, Craft* parent) : type_(type), parent_craft_(parent) {}
    virtual ~Module() = default;
    MODULE_TYPE GetType() const { return type_; }
    virtual void Update(float delta_time) = 0; // Чисто виртуальная функция для обновления модуля
    Craft* GetParentCraft() const { return parent_craft_; }
};

class StorageModule : public Module
{
    std::unordered_map<RESOURCE_TYPE, float> inventory;

public:
    StorageModule(Craft* parent) : Module(STORAGE, parent) {}
    const std::unordered_map<RESOURCE_TYPE, float>& GetInventory() const { return inventory; }
    float GetResourceAmount(RESOURCE_TYPE resource) const;
    void AddResource(RESOURCE_TYPE resource, float amount);
    void RemoveResource(RESOURCE_TYPE resource, float amount);

    void Update(float delta_time) override;
};

class MiningModule : public Module
{
    Asteroid* target_asteroid = nullptr;
    StorageModule* storage_module = nullptr;
public:
    MiningModule(Craft* parent) : Module(MINING, parent) {};
    void MineResource(Asteroid* asteroid, StorageModule* storage) {
        target_asteroid = asteroid;
        storage_module = storage;
    };
    void Update(float delta_time) override;
};

class PropulsionModule : public Module
{
    float acceleration;
    Vector2 target;
public:
    PropulsionModule(Craft* parent, float acceleration) : Module(PROPULSION, parent), acceleration(acceleration) {}
    float GetAcceleration() const { return acceleration; }
    void FlyTo(const Vector2& target) { this->target = target; }
    const Vector2& GetTarget() const { return target; }
    void Update(float delta_time) override;
};


class Craft : public Entity
{
    std::vector<std::unique_ptr<Module>> modules;
public:
    const std::vector<std::unique_ptr<Module>>& GetModules() const { return modules; }
    void AddModule(std::unique_ptr<Module> module) { modules.push_back(std::move(module)); }

    Craft(std::size_t id) : Entity(CRAFT, id) {}
    Craft(const Vector2& pos, const Vector2& vel, std::size_t id)
        : Entity(pos, vel, CRAFT, id) {}
    
    void Update(float delta_time) override
    {
        for (const auto& module : modules) {
            module->Update(delta_time);
        }
    }
};


class World
{
    mutable std::mutex mutex_;
    std::unordered_map<std::size_t, Entity*> entities;
    std::unordered_map<std::size_t, Entity*> asteroids;
    std::unordered_map<std::size_t, Entity*> crafts;

    Vector2 size{1000.0f, 1000.0f};

    std::size_t next_entity_id = 1;

public:
    std::mutex& GetMutex() const { return mutex_; }
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
void SaveWorld(const World* world, const std::string& filename);

void UpdateWorld(World* world, float delta_time);

Vector2 GetRandomPosition(const Vector2& size);
