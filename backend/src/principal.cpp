#include "principal.hpp"

#include <stdexcept>
#include <utility>

Principal::Principal(std::int64_t id, std::string name)
    : user_id(id), username(std::move(name)) {}

const Principal* Principal::Server() {
    static const Principal server(SERVER_USER_ID, "server");
    return &server;
}

const Principal* PrincipalRegistry::Add(std::int64_t user_id, const std::string& username) {
    if (user_id <= SERVER_USER_ID) throw std::invalid_argument("Player ID must be positive");
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = players_.find(user_id);
    if (found != players_.end()) {
        if (found->second->username != username)
            throw std::runtime_error("Principal registry already contains a different account for this ID");
        return found->second.get();
    }
    auto player = std::unique_ptr<const Principal>(new Principal(user_id, username));
    const auto* result = player.get();
    players_.emplace(user_id, std::move(player));
    return result;
}

const Principal* PrincipalRegistry::Find(std::int64_t user_id) const {
    if (user_id == SERVER_USER_ID) return Principal::Server();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = players_.find(user_id);
    return found == players_.end() ? nullptr : found->second.get();
}
