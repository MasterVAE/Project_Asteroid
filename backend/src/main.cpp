#include <iostream>
#include <ostream>
#include <string>

#include "world.hpp"
#include "webserver.hpp"


int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <world_file> [web_root]\n";
        return 1;
    }
    World* world = LoadWorld(argv[1]);
    if(!world) return 1;

    LaunchWebServer(world, argc > 2 ? argv[2] : "frontend/browser");

    while(true)
    {
        UpdateWorld(world);
    }
}
