#pragma once

#include <cstdint>
#include <string>

class World;
class Player;

// One live WebSocket. Automatically registers/unregisters itself in World.
class Connection {
public:
    explicit Connection(World& world);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    uint64_t id = 0; // Connection ID, independent of player ID.
    Player* player = nullptr; // nullptr = not logged in.

    bool IsLoggedIn() const;
    bool Login(uint32_t player_id, const std::string& username);
    void Logout();

private:
    World& world_;
};
