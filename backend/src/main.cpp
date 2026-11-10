#include <iostream>
#include <thread>
#include <chrono>

#include "game_server.hpp"
#include "web_server.hpp"

int main() {

    std::thread([] {
        World* world = StartGameServer();
        using Clock = std::chrono::steady_clock;
        auto last_time = Clock::now();
        while (true) {
            const auto current_time = Clock::now();
            const float delta_time = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;
            UpdateGameServer(world, delta_time);
            std::this_thread::sleep_until(current_time + std::chrono::milliseconds(16));
        }
    }).detach();
    try {
        RunServer();
    } catch (const std::exception& error) {
        std::cerr << "Server: " << error.what() << '\n';
        return 1;
    }
}
