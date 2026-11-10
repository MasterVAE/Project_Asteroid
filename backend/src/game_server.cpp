#include <iostream>

#include "game_server.hpp"


World* StartGameServer()
{
    std::cout << "Game server started" << std::endl;
    return new World();
}

void UpdateGameServer(World* world, float delta_time)
{
    for (auto entity : world->entities)
    {
        entity.Update(delta_time);
    }
}