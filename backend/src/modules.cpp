#include "modules.hpp"
#include "game.hpp"

#include <cmath>
#include <mutex>
#include <stdexcept>
#include <iostream>

// prop module


void PropulsionModule::Update(float delta_time)
{
    std::lock_guard lock(parent_->state_mutex_);
    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * throttle_ * delta_time * parent_->Forward());
}

void PropulsionModule::SetThrottle(float throttle)
{
    std::lock_guard lock(parent_->state_mutex_);
    if (throttle >= 0 && throttle <= 1) throttle_ = throttle;
}


boost::json::object PropulsionModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_max_throttle")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
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
            SetThrottle(value);
        }
    }
    return boost::json::object{};
}



// storage module



std::size_t StorageModule::ResourceCount(RESOURCE_TYPE resource)
{
    const auto it = inventory_.find(resource);
    return it == inventory_.end() ? 0 : it->second;
}

void StorageModule::AddToInventory(std::unordered_map<RESOURCE_TYPE, std::size_t> add)
{
    std::size_t used = 0;

    // Check each count before summing to avoid overflowing std::size_t.
    for (const auto& [resource, count] : inventory_)
    {
        if (count > capacity_ - used)
            throw std::runtime_error("Storage capacity exceeded");
        used += count;
    }
    for (const auto& [resource, count] : add)
    {
        if (count > capacity_ - used)
            throw std::runtime_error("Not enough storage capacity");
        used += count;
    }

    // Commit only after the entire addition succeeds, including allocations.
    auto updated = inventory_;
    for (const auto& [resource, count] : add)
    {
        if (count != 0)
            updated[resource] += count;
    }
    inventory_.swap(updated);
}

void StorageModule::TakeFromInventory(std::unordered_map<RESOURCE_TYPE, std::size_t> take)
{
    for (const auto& [resource, count] : take)
    {
        if (count > ResourceCount(resource))
            throw std::runtime_error("Not enough resources in storage");
    }

    for (const auto& [resource, count] : take)
    {
        if (count == 0)
            continue;

        auto it = inventory_.find(resource);
        it->second -= count;
        if (it->second == 0)
            inventory_.erase(it);
    }
}

void StorageModule::Update(float /*delta_time*/) {}

boost::json::object StorageModule::Handle(boost::json::object /*request*/)
{
    return {};
}



// RCS module





void RCSModule::Update(float delta_time)
{
    std::lock_guard lock(parent_->state_mutex_);
    parent_->SetAngularVelocity(gyro_ * max_gyro_);
    parent_->SetVelocity(parent_->GetVelocity() + max_thrust_ * parent_->FromLocalTowWorld(thrust_) * delta_time);
}

void RCSModule::SetGyro(float gyro)
{
    std::lock_guard lock(parent_->state_mutex_);
    if (gyro >= -1 && gyro <= 1) gyro_ = gyro;
}

void RCSModule::SetThrust(Vector2 thrust)
{
    std::lock_guard lock(parent_->state_mutex_);
    if(thrust.Length() <= 1) thrust_ = thrust;
    else thrust_ = thrust.Normalized();
}

boost::json::object RCSModule::Handle(boost::json::object request)
{
    std::string method = boost::json::value_to<std::string>(request.at("method"));
    if(method == "get_max_gyro" || method == "get_max_thrust")
    {
        boost::json::object result;
        {
            std::lock_guard lock(parent_->state_mutex_);
            if(method == "get_max_gyro")
                result["max_gyro"] = max_gyro_;
            else 
                result["max_thrust"] = max_thrust_;
        }

        return boost::json::object{
            {"type", "response"},
            {"id", request.at("id")},
            {"result", result}
        };
    }
    if(method == "set_gyro")
    {
        double value = boost::json::value_to<double>(request.at("value"));

        if (std::isfinite(value)) {
            SetGyro(value);
        }
    }
    if (method == "set_thrust")
    {
        const auto& value = request.at("value").as_array();
    
        if (value.size() != 2)
            throw std::invalid_argument("Thrust must contain [x, y]");
    
        float x = boost::json::value_to<float>(value.at(0));
        float y = boost::json::value_to<float>(value.at(1));
    
        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::invalid_argument("Thrust must be finite");
    
        SetThrust(Vector2(x, y));
    }
    return boost::json::object{};
}
