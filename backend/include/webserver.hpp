#pragma once

#include <boost/json/fwd.hpp>
#include <string>

class World;
class Entity;

void LaunchWebServer(World* world, const std::string& web_root = "frontend/browser");

boost::json::value GetMap(const World* world);
boost::json::value GetEntity(const Entity* entity);
