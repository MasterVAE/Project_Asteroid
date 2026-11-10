#include "connection.hpp"
#include "game_server.hpp"

Connection::Connection(World& world) : world_(world) {
    std::lock_guard<std::mutex> lock(world_.mutex);
    id = world_.next_connection_id_++;
    world_.connections.emplace(id, this);
}

Connection::~Connection() {
    std::lock_guard<std::mutex> lock(world_.mutex);
    if (player) player->connection = nullptr;
    player = nullptr;
    world_.connections.erase(id);
}

bool Connection::IsLoggedIn() const {
    std::lock_guard<std::mutex> lock(world_.mutex);
    return player != nullptr;
}

bool Connection::Login(uint32_t player_id, const std::string& username) {
    std::lock_guard<std::mutex> lock(world_.mutex);
    if (player) return false;
    // Existing players are loaded at startup; registration adds a new one here.
    auto result = world_.players.try_emplace(player_id, Player{player_id, username, nullptr});
    Player& target = result.first->second;
    if (target.connection) return false; // A player can use only one connection.
    world_.EnsureCraft(target);
    player = &target;
    target.connection = this;
    return true;
}

void Connection::Logout() {
    std::lock_guard<std::mutex> lock(world_.mutex);
    if (player) player->connection = nullptr;
    player = nullptr;
}
