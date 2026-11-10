#pragma once

#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "player.hpp"
#include "vector.hpp"
#include "connection.hpp"
#include "script_runtime.hpp"

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
                                            owner_(owner), id_(id), type_(type), position_(pos) {};
    virtual ~Entity() = default;

    Vector2 GetPosition() const { return position_; }
    void SetPosition(Vector2 pos) { position_ = pos; }
    Vector2 GetVelocity() const { return velocity_; }
    void SetVelocity(Vector2 vel) { velocity_ = vel; }
    std::size_t GetId() const { return id_; }
    Player* GetOwner() const { return owner_; }
    ENTITY_TYPE GetType() const { return type_; }

    virtual void Update(float) {}
};

class Module
{

};

class Craft : public Entity
{
    std::vector<Module> modules;
public:
    std::string script_source;
    std::shared_ptr<ScriptRuntime> script;
    Craft(Player* owner, std::size_t id, Vector2 pos) 
                                                : Entity(owner, id, pos, ENTITY_TYPE::CRAFT) {};

    std::vector<Module>& GetModues() { return modules; }
    const std::vector<Module>& GetModules() const { return modules; }

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
    // Caller holds mutex. One starter craft per registered player.
    void EnsureCraft(Player& player);

private:
    friend class Connection;
    uint64_t next_connection_id_ = 1;
    uint32_t next_craft_id_ = 1;
};

World* StartGameServer();
void UpdateGameServer(World* world, float delta_time);
std::vector<uint8_t> EncodeWorld(World& world);
// Called under world.mutex. Keep it nonblocking; nullopt means no reply.
std::optional<std::string> HandleScriptMessage(World& world, Craft& craft, const std::string& message);
