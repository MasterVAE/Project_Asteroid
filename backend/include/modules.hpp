#pragma once

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <deque>
#include <string>
#include <unordered_map>

#include "resources.hpp"
#include "vector.hpp"

class Entity;
class Craft;

enum MODULE_TYPE
{
    NONE,
    PROPULSION,
    STORAGE,
    RCS,
    DRILL,
    SCANNER,
    POWER,
    FACTORY,
    SHIPYARD,
    FUEL_TANK
};

class Module
{
    std::string name_;
    MODULE_TYPE type_;

protected:
    Craft* parent_;

public:
    Module(Craft* parent, MODULE_TYPE type, std::string name)
        : name_(name), type_(type), parent_(parent) {}
    virtual ~Module() = default;

    std::string GetName() { return name_; }
    void SetName(std::string name) { name_ = name; }
    MODULE_TYPE GetType() { return type_; }

    virtual boost::json::object Handle(boost::json::object) { return {}; };
    virtual void Update(float) {};

};


class PropulsionModule : public Module
{
    float max_thrust_ = 500;
    float throttle_ = 0;
    float fuel_consumption_per_second_ = 10;
public:
    PropulsionModule(Craft* parent) : Module(parent, MODULE_TYPE::PROPULSION, "") {}

    float GetThrottle();
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

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};


class RCSModule : public Module
{
    float max_gyro_ = 100;
    float gyro_ = 0;
    float max_thrust_ = 30;
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
    float power_consuption_ =10;
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
    ScanModule(Craft* parent) : Module(parent, MODULE_TYPE::SCANNER, "") {}

    struct Result
    {
        std::size_t id;
        std::string name;
        std::string type;
        Vector2 position;
    };

    std::vector<Result> Scan();
    void Signal(std::size_t id, const std::string& message);

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};

class PowerModule : public Module
{
    friend class FactoryModel;
    friend class ShipyardModule;

    float capacity_ = 100;
    float charge_ = 100;
    float generation_per_second_ = 5;
    bool TakeChargeLocked(float charge); // Requires the craft state mutex.
public:
    PowerModule(Craft* parent) : Module(parent, MODULE_TYPE::POWER, "") {}

    bool HasCharge(float need) { return charge_ >= need; }
    bool TakeCharge(float charge);

    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};


class FactoryModel : public Module
{
    float production_cooldown_ = 3;
    float energy_consuption_per_second_ = 20;
    float time_ = 0;

    std::deque<RECIPE> queue_;

public:
    FactoryModel(Craft* parent) : Module(parent, MODULE_TYPE::FACTORY, "") {}

    const std::vector<RECIPE>& GetRecipes() const;

    void Produce(RECIPE recipe);
    void Update(float delta_time) override;

    boost::json::object Handle(boost::json::object request) override;
};

class ShipyardModule : public Module
{
    struct Order
    {
        std::size_t craft_id;
        MODULE_TYPE type; // NONE constructs a new craft.
    };

    float production_cooldown_ = 3;
    float energy_consuption_per_second_ = 30;
    float building_radius_ = 20;
    float time_ = 0;
    std::deque<Order> queue_;

public:
    ShipyardModule(Craft* parent) : Module(parent, MODULE_TYPE::SHIPYARD, "") {}

    void ConstructCraft();
    void AddModule(std::size_t craft_id, MODULE_TYPE type);

    void Update(float delta_time) override;
    boost::json::object Handle(boost::json::object request) override;
};

class FuelTankModule : public Module
{
    friend class PropulsionModule;

    float capacity_ = 100;
    float fuel_ = 0;
    float fuel_per_object_ = 20; 
    bool GetFuelLocked(float fuel); // Requires the craft state mutex.
public:
    FuelTankModule(Craft* parent) : Module(parent, MODULE_TYPE::FUEL_TANK, "") {}

    void SetFuel(float fuel);

    bool GetFuel(float fuel);

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
    {PROPULSION, "propulsion", 10},
    {STORAGE, "storage", 7},
    {RCS, "rcs", 5},
    {DRILL, "drill", 15},
    {SCANNER, "scanner", 5},
    {POWER, "power", 20},
    {FACTORY, "factory", 20},
    {SHIPYARD, "shipyard", 50},
    {FUEL_TANK, "fuel_tank", 5}
};

static const std::size_t MODULES_COUNT = sizeof(MODULES_DATA)/sizeof(MODULES_DATA[0]);


// Shipyard recipes create a craft or install a module, rather than produce
// inventory items. Repeated input entries mean multiple units of a resource.
struct CONSTRUCTION_RECIPE
{
    std::vector<RESOURCE_TYPE> input;
    MODULE_TYPE output;
};

// An empty craft; modules are installed separately using MODULE_RECIPES.
inline const CONSTRUCTION_RECIPE CRAFT_RECIPE
{
    {STEEL}, MODULE_TYPE::NONE
};

// Output names match MODULES_DATA, including the existing spelling "scanner".
inline const CONSTRUCTION_RECIPE MODULE_RECIPES[] =
{
    {{STEEL},                   MODULE_TYPE::PROPULSION},
    {{STEEL},                                  MODULE_TYPE::STORAGE},
    {{STEEL},                           MODULE_TYPE::RCS},
    {{STEEL},                        MODULE_TYPE::DRILL},
    {{STEEL},                       MODULE_TYPE::SCANNER},
    {{STEEL},                   MODULE_TYPE::POWER},
    {{STEEL},         MODULE_TYPE::FACTORY},
    {{STEEL},   MODULE_TYPE::SHIPYARD},
    {{STEEL},                                  MODULE_TYPE::FUEL_TANK}
};

static const std::size_t MODULE_RECIPES_COUNT = sizeof(MODULE_RECIPES)/sizeof(MODULE_RECIPES[0]);
