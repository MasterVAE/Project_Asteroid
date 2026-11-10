#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

#include "game_server.hpp"
#include "web_server.hpp"

int main() {
    std::signal(SIGPIPE, SIG_IGN); // A stopped script may close its pipe at any time.

    World* world = StartGameServer();
    std::thread([world] {
        using Clock = std::chrono::steady_clock;
        auto last_time = Clock::now();
        while (true) {
            const auto current_time = Clock::now();
            const float delta_time = std::chrono::duration<float>(current_time - last_time).count();
            last_time = current_time;
            UpdateGameServer(world, delta_time);
            std::this_thread::sleep_until(current_time + std::chrono::milliseconds(50));
        }
    }).detach();
    try {
        RunServer(*world);
    } catch (const std::exception& error) {
        std::cerr << "Server: " << error.what() << '\n';
        return 1;
    }
}
