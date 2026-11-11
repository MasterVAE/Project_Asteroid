#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <thread>
#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/json.hpp>
#include <vector>
#include <cmath>

#include "vector.hpp"
#include "modules.hpp"


class Entity
{
private:
    std::size_t id_ = 0;
    Vector2 position_;
    Vector2 velocity_;
    float rotation_ = 0.0f;
    float angular_velocity_ = 0.0f;

public:
    // ID
    std::size_t GetId() const { return id_; }
    void SetId(std::size_t id) { id_ = id; }

    // Position
    Vector2 GetPosition() const { return position_; }
    void SetPosition(const Vector2& pos) { position_ = pos; }

    // Velocity
    Vector2 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector2& vel) { velocity_ = vel; }

    // Rotation
    float GetRotation() const { return rotation_; }
    void SetRotation(float rotation) { 
        rotation_ = rotation; 
        if(rotation_ < 0) rotation_ += 2 * M_PI;
        else if(rotation_ >= 2 * M_PI) rotation_ -= 2 * M_PI;
    }

    // Angular velocity
    float GetAngularVelocity() const { return angular_velocity_; }
    void SetAngularVelocity(float a_vel) { angular_velocity_ = a_vel; }

    Entity();
    Entity(Vector2 pos, Vector2 vel, float rot, float a_vel);
    virtual ~Entity() = default;

    virtual void Update(float delta_time) {};

    Vector2 Forward();
};





class Craft : public Entity {
public:
    Craft();
    Craft(Vector2 pos);
    ~Craft() override;

    std::vector<std::unique_ptr<Module>>& GetModules() { return modules_; }
    Module& AddModule(MODULE_TYPE type);

    void LaunchScript(std::string filename);
    void SendEvent(const boost::json::object& event_data);

    void Update(float delta_time) override;

    std::mutex send_mutex_;
    std::mutex state_mutex_;

private:
    std::size_t module_index_;

    std::vector<std::unique_ptr<Module>> modules_;

    void Send(const boost::json::object& message);
    void StopScript();

    boost::asio::io_context io_;
    std::optional<boost::process::v2::popen> process_;

    std::thread reader_;
};





class World
{
    std::size_t next_id_ = 1;
    std::unordered_map<std::size_t, std::unique_ptr<Entity>> entities_;

public:
    World();
    World(std::string filename);

    void SpawnEntity(std::unique_ptr<Entity> entity);

    std::unordered_map<std::size_t, std::unique_ptr<Entity>>& GetEntities() { return entities_; }

    void Update(float delta_time);
};
