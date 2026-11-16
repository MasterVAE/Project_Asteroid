#pragma once

#ifdef _WIN32
// Boost.Process 1.87 must see the CRT declaration of _get_osfhandle first.
#include <io.h>
#endif

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
    const float mass_ = 100.0f;
    float rotation_ = 0.0f;
    float angular_velocity_ = 0.0f;
    ENTITY_TYPE type_;

    std::string name_;

public:
    World* world = nullptr;

    // ID
    std::size_t GetId() const;
    void SetId(std::size_t id);

    // Name
    void SetName(std::string name);
    std::string GetName() const;

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

    Entity(ENTITY_TYPE type, std::string name = "");
    Entity(ENTITY_TYPE type, Vector2 pos, Vector2 vel, float rot, float a_vel, std::string name = "");
    virtual ~Entity() = default;

    ENTITY_TYPE GetType() { return type_; }

    virtual void Update(float) {};
    virtual void Advance(float delta_time);

    virtual float GetMass();

    Vector2 Forward();
    Vector2 FromLocalTowWorld(Vector2 local);
};





class Craft : public Entity {
    friend class PropulsionModule;
    friend class RCSModule;
    friend class StorageModule;
    friend class ShipyardModule;
    friend class GrabModule;
    friend class Asteroid;
    friend class World;

public:
    // Connection fields are protected by connection_mutex_. One partner per craft.
    Craft* connected = nullptr;
    bool readyToConnect = false;

    Craft();
    Craft(Vector2 pos, std::string name = "Craft");
    ~Craft() override;

    std::vector<Module*> GetModules();
    std::vector<Module*> GetModulesOfType(MODULE_TYPE type);
    Module& AddModule(MODULE_TYPE type);

    void LaunchScript(std::string filename);
    bool IsScriptRunning();
    void SendEvent(const boost::json::object& event_data);
    float GetMass() override;
    bool Connect(Craft* target);
    void Disconnect();
    void SetReadyToConnect(bool ready);
    boost::json::object GetConnection();

    void Advance(float delta_time) override;
    void Update(float delta_time) override;

    std::mutex send_mutex_;
    std::mutex state_mutex_;

private:
    static std::mutex connection_mutex_;
    struct AssemblyLock
    {
        std::unique_lock<std::mutex> connection;
        std::unique_lock<std::mutex> own;
        std::unique_lock<std::mutex> partner;
    };
    AssemblyLock LockAssemblyState();
    std::unordered_map<MODULE_TYPE, std::size_t> module_indices_;

    float defaul_mass_ = 10;
    std::string script_data; // Protected by state_mutex_; persisted with the world.

    std::vector<std::unique_ptr<Module>> modules_;

    float GetOwnMassLocked(); // Requires state_mutex_ to be held.
    float GetMassLocked(); // Requires LockAssemblyState().
    void ApplyImpulseLocked(Vector2 impulse); // Requires LockAssemblyState().
    void SyncGrabbedAsteroidsLocked(); // Requires the connection mutex and state_mutex_.
    Module& AddModuleLocked(MODULE_TYPE type); // Requires state_mutex_ to be held.
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
    std::string script_path_; // Last attached script; protected by send_mutex_.
    bool close_stdin_when_idle_ = false; // Accessed only by the I/O thread.

    std::thread reader_;
};


class Asteroid : public Entity
{
    friend class GrabModule;
    friend class World;
    GrabModule* grabbed_by_ = nullptr; // Protected by Craft::connection_mutex_.
    std::unordered_map<RESOURCE_TYPE, std::size_t> resources_;
public:
    Asteroid();
    Asteroid(Vector2 pos, std::string name = "Asteroid");
    ~Asteroid() override;

    std::unordered_map<RESOURCE_TYPE, std::size_t>& GetResources() { return resources_; }
    RESOURCE_TYPE Mine();

    void Advance(float delta_time) override;
    void Update(float delta_time) override;
};


class World
{
    struct CraftCommand
    {
        std::size_t source;
        std::optional<std::size_t> target; // With an empty launch path, no target means disconnect.
        std::string launch; // A non-empty path means launch on the connected craft.
        std::string grab_module; // A module name means grab target, or release if target is empty.
    };
    std::mutex craft_commands_mutex_;
    std::vector<CraftCommand> craft_commands_;
    void ProcessCraftCommands();
    void StopScripts();
    std::size_t next_id_ = 0;
    std::unordered_map<std::size_t, std::unique_ptr<Entity>> entities_;
    std::unordered_set<std::size_t> pending_destroy_;
    std::vector<std::unique_ptr<Entity>> pending_spawn_;
    bool updating_ = false;

public:
    World();
    ~World();

    void QueueConnection(std::size_t source, std::optional<std::size_t> target);
    void QueueLaunch(std::size_t source, std::string filename);
    void QueueGrab(std::size_t source, std::string module, std::optional<std::size_t> target);

    void SpawnEntity(std::unique_ptr<Entity> entity);
    void DestroyEntity(Entity* entity);

    void Start();
    void Restart();
    void Load(std::string filename);
    void Save(std::string filename);

    std::unordered_map<std::size_t, std::unique_ptr<Entity>>& GetEntities() { return entities_; }

    void Update(float delta_time);
};
