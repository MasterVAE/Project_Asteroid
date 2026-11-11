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

    virtual boost::json::object Handle(boost::json::object request) {};
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

    void SetThrottle(float throttle)
    {
        if(throttle >= 0 && throttle <= 1) throttle_ = throttle;
    }
    void SetGyro(float gyro)
    {
        if(gyro >= -1 && gyro <= 1) gyro_ = gyro;
    }

    void Update(float delta_time);

    boost::json::object Handle(boost::json::object request) override;
};
