#include <iostream>
#include <thread>
#include <chrono>

#include "game_server.hpp"
#include "web_server.hpp"

int main() {

    std::thread serverThread(RunServer);
    serverThread.detach(); 


    World* world = StartGameServer();

    using Clock = std::chrono::steady_clock;
    auto last_time = Clock::now();

    while (true) {
        auto current_time = Clock::now();

        float delta_time = std::chrono::duration<float>(
            current_time - last_time
        ).count();

        last_time = current_time;

        UpdateGameServer(world, delta_time);
    }
}