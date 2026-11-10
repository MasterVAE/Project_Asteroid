#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "player.hpp"
#include "vector.hpp"
#include "connection.hpp"

enum ENTITY_TYPE
{
    ASTEROID,
    CRAFT
};

class Entity
{
    Player* owner_;
    std::size_t id_;
    ENTITY_TYPE type_;

    Vector2 position_;
    Vector2 velocity_;

public:
    Entity(Player* owner, std::size_t id, Vector2 pos, ENTITY_TYPE type) : 
                                            owner_(owner), id_(id), position_(pos), type_(type) {}; 

    Vector2 GetPosition() { return position_; }
    void SetPosition(Vector2 pos) { position_ = pos; }
    Vector2 GetVelocity() { return velocity_; }
    void SetVelocity(Vector2 vel) { velocity_ = vel; }
    std::size_t GetId() { return id_; };
    Player* GetOwner() { return GetOwner(); }

    virtual void Update(float delta_time) {};
};

class Module
{

};

class Craft : Entity
{
    std::vector<Module> modules;
public:
    Craft(Player* owner, std::size_t id, Vector2 pos) 
                                                : Entity(owner, id, pos, ENTITY_TYPE::CRAFT) {};

    std::vector<Module>& GetModues() { return modules; }

    void Update(float delta_time) override;
};


class World
{
public:
    // Lock this mutex when reading/changing the containers or player data.
    std::mutex mutex;
    std::vector<Entity> entities;
    std::vector<Craft> crafts;
    std::unordered_map<uint32_t, Player> players; // All registered players, including offline.
    std::unordered_map<uint64_t, Connection*> connections; // All live WebSockets, including guests.

private:
    friend class Connection;
    uint64_t next_connection_id_ = 1;
};

World* StartGameServer();
void UpdateGameServer(World* world, float delta_time);
