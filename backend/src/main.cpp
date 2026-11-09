#include <chrono>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>

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

    std::thread web_server([world, web_root = std::string(argc > 2 ? argv[2] : "frontend/browser")] {
        LaunchWebServer(world, web_root);
    });

    auto previous_time = std::chrono::steady_clock::now();
    while(true)
    {
        const auto current_time = std::chrono::steady_clock::now();
        const float delta_time = std::chrono::duration<float>(current_time - previous_time).count();
        previous_time = current_time;
        std::lock_guard<std::mutex> lock(world->GetMutex());
        UpdateWorld(world, delta_time);
    }
}
