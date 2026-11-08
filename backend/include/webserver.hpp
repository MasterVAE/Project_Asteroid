#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

#include "world.hpp"

void LaunchWebServer(World* world);

boost::json::value GetMap(const World* world);
boost::json::value GetEntity(const Entity* entity);