#pragma once

#include <boost/json/value.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

struct sqlite3;

class ApiError : public std::runtime_error {
public:
    int status;
    std::string code;
    ApiError(int status, std::string code, const std::string& message);
};

struct Principal {
    std::int64_t user_id;
    std::int64_t credential_id;
    bool is_session;
};

class AuthStore {
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    std::string dummy_password_;

    std::string IssueSession(std::int64_t user_id);
public:
    explicit AuthStore(const std::string& path);
    ~AuthStore();
    AuthStore(const AuthStore&) = delete;
    AuthStore& operator=(const AuthStore&) = delete;

    std::string Register(const std::string& username, const std::string& password);
    std::string Login(const std::string& username, const std::string& password);
    std::optional<Principal> Authenticate(const std::string& token);
    boost::json::value Profile(const Principal& principal);
    void Logout(const Principal& principal);
    boost::json::value CreateKey(const Principal& principal, const std::string& name,
                                int days, const std::string& password);
    boost::json::value ListKeys(const Principal& principal);
    void RevokeKey(const Principal& principal, std::int64_t id);
};
