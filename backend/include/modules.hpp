#pragma once

#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/json.hpp>
#include <string>

class Entity;
class Craft;

enum MODULE_TYPE
{
    PROPULSION
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

    virtual boost::json::object Handle(boost::json::object request) { return {}; };
    virtual void Update(float delta_time) {};

};


class PropulsionModule : public Module
{
    float max_thrust_ = 10;
    float throttle_ = 0;
    float max_gyro_ = 1;
    float gyro_=0;
public:
    PropulsionModule(Craft* parent) : Module(parent, MODULE_TYPE::PROPULSION, "") {}

    void SetThrottle(float throttle);
    void SetGyro(float gyro);

    void Update(float delta_time);

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
    {PROPULSION, "prop", 1}
};

static const std::size_t MODULES_COUNT = sizeof(MODULE_DATA)/sizeof(MODULES_DATA[0]);
