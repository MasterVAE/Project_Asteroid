#pragma once

#include <boost/json/value.hpp>

class World;
class Entity;

void LaunchWebServer(World* world);

boost::json::value GetMap(const World* world);
boost::json::value GetEntity(const Entity* entity);
