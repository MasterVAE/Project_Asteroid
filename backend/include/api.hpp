#pragma once

#include "auth.hpp"

#include <boost/json/value.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

class World;

struct ApiReply {
    int status = 200;
    boost::json::value body;
    std::string session_token;
    bool clear_session = false;
};

ApiReply ApiFailure(int status, const std::string& code, const std::string& message);

class ApiService {
    struct Attempts { std::chrono::steady_clock::time_point start; unsigned count = 0; };
    AuthStore& auth_;
    World& world_;
    std::mutex attempts_mutex_;
    std::unordered_map<std::string, Attempts> attempts_;
    void LimitAttempt(const std::string& identity);
public:
    ApiService(AuthStore& auth, World& world) : auth_(auth), world_(world) {}
    ApiReply Handle(const std::string& body, const std::string& credential,
                    const std::string& client_ip, bool http);
};
