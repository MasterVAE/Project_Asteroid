#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <mutex>
#include <thread>
#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/json.hpp>
#include <vector>
#include <cmath>
#include <deque>

#include "vector.hpp"
#include "modules.hpp"
#include "resources.hpp"


enum ENTITY_TYPE
{
    CRAFT,
    ASTEROID
};


class World;

class Entity
{
private:
    mutable std::mutex entity_mutex_;
    std::size_t id_ = 0;
    Vector2 position_;
    Vector2 velocity_;
    float rotation_ = 0.0f;
    float angular_velocity_ = 0.0f;
    ENTITY_TYPE type_;

public:
    World* world = nullptr;

    // ID
    std::size_t GetId() const;
    void SetId(std::size_t id);

    // Position
    Vector2 GetPosition() const;
    void SetPosition(const Vector2& pos);

    // Velocity
    Vector2 GetVelocity() const;
    void SetVelocity(const Vector2& vel);

    // Rotation
    float GetRotation() const;
    void SetRotation(float rotation);

    // Angular velocity
    float GetAngularVelocity() const;
    void SetAngularVelocity(float a_vel);

    Entity(ENTITY_TYPE type);
    Entity(ENTITY_TYPE type, Vector2 pos, Vector2 vel, float rot, float a_vel);
    virtual ~Entity() = default;

    ENTITY_TYPE GetType() { return type_; }

    virtual void Update(float) {};
    void Advance(float delta_time);

    Vector2 Forward();
    Vector2 FromLocalTowWorld(Vector2 local);
};





class Craft : public Entity {
public:
    Craft();
    Craft(Vector2 pos);
    ~Craft() override;

    std::vector<Module*> GetModules();
    Module& AddModule(MODULE_TYPE type);

    void LaunchScript(std::string filename);
    bool IsScriptRunning();
    void SendEvent(const boost::json::object& event_data);

    void Update(float delta_time) override;

    std::mutex send_mutex_;
    std::mutex state_mutex_;

private:
    std::size_t module_index_;

    std::vector<std::unique_ptr<Module>> modules_;

    void Send(const boost::json::object& message);
    void StopScript();
    void ReadPacket();
    void HandlePacket(const std::string& line);
    void QueuePacket(std::string packet);
    void WritePacket();

    boost::asio::io_context io_;
    std::optional<boost::process::v2::popen> process_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    boost::asio::streambuf read_buffer_;
    std::deque<std::string> outgoing_;
    bool stopping_ = true; // Protected by send_mutex_.
    bool close_stdin_when_idle_ = false; // Accessed only by the I/O thread.

    std::thread reader_;
};


class Asteroid : public Entity
{
    std::unordered_map<RESOURCE_TYPE, std::size_t> resources_;
public:
    Asteroid();
    Asteroid(Vector2 pos);
    ~Asteroid() override = default;

    std::unordered_map<RESOURCE_TYPE, std::size_t>& GetResources() { return resources_; }
    RESOURCE_TYPE Mine();

    void Update(float delta_time) override;
};


class World
{
    std::size_t next_id_ = 1;
    std::unordered_map<std::size_t, std::unique_ptr<Entity>> entities_;
    std::unordered_set<std::size_t> pending_destroy_;
    bool updating_ = false;

public:
    World();
    World(std::string filename);

    void SpawnEntity(std::unique_ptr<Entity> entity);
    void DestroyEntity(Entity* entity);

    std::unordered_map<std::size_t, std::unique_ptr<Entity>>& GetEntities() { return entities_; }

    void Update(float delta_time);
};
