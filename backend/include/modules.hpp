#pragma once

#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/json.hpp>
#include <string>
#include <unordered_map>

#include "resources.hpp"
#include "vector.hpp"

class Entity;
class Craft;

enum MODULE_TYPE
{
    PROPULSION,
    STORAGE,
    RCS,
    DRILL,
    SCAN,
    POWER
};

class Module
{
    std::string name_;
    MODULE_TYPE type_;

protected:
    Craft* parent_;

public:
    Module(Craft* parent, MODULE_TYPE type, std::string name)
        : parent_(parent), name_(name), type_(type) {}
    virtual ~Module() = default;

    std::string GetName() { return name_; }
    void SetName(std::string name) { name_ = name; }
    MODULE_TYPE GetType() { return type_; }

    virtual boost::json::object Handle(boost::json::object) { return {}; };
    virtual void Update(float) {};

};


class PropulsionModule : public Module
{
    float max_thrust_ = 10;
    float throttle_ = 0;
public:
    PropulsionModule(Craft* parent) : Module(parent, MODULE_TYPE::PROPULSION, "") {}

    void SetThrottle(float throttle);

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};


class StorageModule : public Module
{
    std::size_t capacity_ = 20;
    std::unordered_map<RESOURCE_TYPE, std::size_t> inventory_;
    bool ready_to_recieve_ = false;
public:
    StorageModule(Craft* parent, std::size_t capacity) 
                            : Module(parent, MODULE_TYPE::STORAGE, ""), capacity_(capacity) {}

    std::unordered_map<RESOURCE_TYPE, std::size_t>& GetInventory() { return inventory_; }
    std::size_t GetCapacity() const { return capacity_; }
    std::size_t ResourceCount(RESOURCE_TYPE);
    void AddToInventory(std::unordered_map<RESOURCE_TYPE, std::size_t> add);
    void TakeFromInventory(std::unordered_map<RESOURCE_TYPE, std::size_t> take);

    void ReadyForRecieve();
    void NotReadyForRecieve();

    void Transfer(std::size_t id, RESOURCE_TYPE type);

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};


class RCSModule : public Module
{
    float max_gyro_ = 1;
    float gyro_ = 0;
    float max_thrust_ = 1;
    Vector2 thrust_;
public:
    RCSModule(Craft* parent) : Module(parent, MODULE_TYPE::RCS, "") {}

    void SetGyro(float gyro);
    void SetThrust(Vector2 thrust);

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};


class DrillModule : public Module
{
    float drill_cooldown_ = 0.5;
    float timer_ = 0;
    bool enabled_ = false;
    float power_consuption_ = 5;
public:
    DrillModule(Craft* parent) : Module(parent, MODULE_TYPE::DRILL, "") {}

    void Enable();
    void Disable();

    float GetDrillCooldown() { return drill_cooldown_; }

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};

class ScanModule : public Module
{
    float radius = 500;
    float scan_power_ = 30;

public:
    ScanModule(Craft* parent) : Module(parent, MODULE_TYPE::SCAN, "") {}

    std::vector<std::pair<std::string, Vector2>> Scan();

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};

class PowerModule : public Module
{
    float capacity_ = 100;
    float charge_ = 100;
    float generation_per_second_ = 5;
public:
    PowerModule(Craft* parent) : Module(parent, MODULE_TYPE::POWER, "") {}

    bool HasCharge(float need) { return charge_ >= need; }
    bool TakeCharge(float charge);

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};

struct MODULE_DATA
{
    MODULE_TYPE type;
    std::string name;
    float mass; 
};

static const MODULE_DATA MODULES_DATA[] = 
{
    {PROPULSION, "prop", 1},
    {STORAGE, "stor", 1},
    {RCS, "rcs", 1},
    {DRILL, "drill", 1},
    {SCAN, "scan", 1},
    {POWER, "power", 1}
};

static const std::size_t MODULES_COUNT = sizeof(MODULES_DATA)/sizeof(MODULES_DATA[0]);
