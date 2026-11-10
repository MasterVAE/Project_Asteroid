#pragma once

#include <cstdint>
#include <string>

class Connection;

class Player {
public:
    uint32_t id = 0; // Same ID as in SQLite.
    std::string username;
    Connection* connection = nullptr; // nullptr = offline.

    // Add game data here: position, inventory, etc.
};
