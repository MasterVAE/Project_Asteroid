#pragma once

#include "principal.hpp"

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

struct AuthContext {
    const Principal* principal;
    std::int64_t credential_id;
    bool is_session;
};

class AuthStore {
    PrincipalRegistry& principals_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    std::string dummy_password_;

    std::string IssueSession(std::int64_t user_id);
public:
    AuthStore(const std::string& path, PrincipalRegistry& principals);
    ~AuthStore();
    AuthStore(const AuthStore&) = delete;
    AuthStore& operator=(const AuthStore&) = delete;

    std::string Register(const std::string& username, const std::string& password);
    std::string Login(const std::string& username, const std::string& password);
    std::optional<AuthContext> Authenticate(const std::string& token);
    const Principal* GetPrincipal(std::int64_t user_id) const { return principals_.Find(user_id); }
    boost::json::value Profile(const AuthContext& context);
    void Logout(const AuthContext& context);
    boost::json::value CreateKey(const AuthContext& context, const std::string& name,
                                int days, const std::string& password);
    boost::json::value ListKeys(const AuthContext& context);
    void RevokeKey(const AuthContext& context, std::int64_t id);
};
