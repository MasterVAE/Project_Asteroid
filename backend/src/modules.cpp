#include "modules.hpp"
#include "game.hpp"

#include <cmath>
#include <mutex>

void PropulsionModule::Update(float delta_time)
{
    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * throttle_ * delta_time * parent_->Forward());
    parent_->SetAngularVelocity(gyro_ * max_gyro_);
}

boost::json::object PropulsionModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_max_gyro" || method == "get_max_throttle")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            if(method == "get_max_gyro")
                result["max_gyro"] = max_gyro_;
            else
                result["max_throttle"] = max_thrust_;
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    if(method == "set_throttle")
    {
        double value = boost::json::value_to<double>(request.at("value"));

        if (std::isfinite(value)) {
            std::lock_guard lock(parent_->state_mutex_);
            SetThrottle(value);
        }
    }
    if(method == "set_gyro")
    {
        double value = boost::json::value_to<double>(request.at("value"));

        if (std::isfinite(value)) {
            std::lock_guard lock(parent_->state_mutex_);
            SetGyro(value);
        }
    }
    return boost::json::object{};
}
