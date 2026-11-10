#pragma once

#include <mutex>
#include <sqlite3.h>
#include "protocol.hpp"

class Accounts {
public:
    explicit Accounts(const std::string& path);
    ~Accounts();
    Accounts(const Accounts&) = delete;
    Accounts& operator=(const Accounts&) = delete;
    protocol::AuthResponse authenticate(const protocol::Credentials& credentials, bool registration);
private:
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
