#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

inline constexpr std::int64_t SERVER_USER_ID = 0;

class PrincipalRegistry;
class AuthStore;

// One immutable identity per account in a world's registry, independent of tokens.
class Principal final {
    friend class PrincipalRegistry;
    Principal(std::int64_t id, std::string name);
public:
    const std::int64_t user_id;
    const std::string username;

    Principal(const Principal&) = delete;
    Principal& operator=(const Principal&) = delete;
    Principal(Principal&&) = delete;
    Principal& operator=(Principal&&) = delete;

    static const Principal* Server();
};

// Owned by World. Entries are never removed or replaced during its lifetime.
class PrincipalRegistry final {
    friend class AuthStore;
    mutable std::mutex mutex_;
    std::unordered_map<std::int64_t, std::unique_ptr<const Principal>> players_;
    const Principal* Add(std::int64_t user_id, const std::string& username);
public:
    const Principal* Find(std::int64_t user_id) const;
};
