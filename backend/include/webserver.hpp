#pragma once

#include <boost/json/fwd.hpp>
#include <string>

class World;
class Entity;
class Principal;

void LaunchWebServer(World* world, const std::string& web_root = "frontend/browser");

boost::json::value GetMap(const World* world);
boost::json::value GetMyEntities(const World* world, const Principal* owner);
boost::json::value GetEntity(const Entity* entity);
boost::json::value CreateEntity(World* world, const Principal* owner);
