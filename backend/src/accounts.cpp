#include "accounts.hpp"
#include "game_server.hpp"

#include <array>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {
constexpr int iterations = 600000;
using Salt = std::array<unsigned char, 16>;
using Hash = std::array<unsigned char, 32>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    return Statement(stmt, sqlite3_finalize);
}
Hash hash_password(const std::string& password, const unsigned char* salt) {
    Hash hash{};
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          salt, Salt{}.size(), iterations, EVP_sha256(),
                          hash.size(), hash.data()) != 1)
        throw std::runtime_error("Password hashing failed");
    return hash;
}
}

Accounts::Accounts(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        const std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db_, 5000);
    const char* sql = "CREATE TABLE IF NOT EXISTS accounts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE,"
        "salt BLOB NOT NULL, password_hash BLOB NOT NULL)";
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        const std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error(error);
    }
}
Accounts::~Accounts() { sqlite3_close(db_); }

void Accounts::LoadPlayers(World& world) {
    std::scoped_lock lock(mutex_, world.mutex);
    auto stmt = prepare(db_, "SELECT id, username FROM accounts");
    int result;
    while ((result = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto id = static_cast<uint32_t>(sqlite3_column_int64(stmt.get(), 0));
        const auto username = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        world.players.try_emplace(id, Player{id, username, nullptr});
    }
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    for (auto& entry : world.players) world.EnsureCraft(entry.second);
}

protocol::AuthResponse Accounts::authenticate(const protocol::Credentials& c, bool registration) {
    using protocol::Status;
    if (c.username.empty() || c.username.size() > 64 ||
        c.username.find_first_of(" \t\r\n") != std::string::npos ||
        c.username.find('\0') != std::string::npos ||
        c.password.size() < 8 || c.password.size() > 256)
        return {Status::InvalidInput, 0, {}, "Логин: 1–64 байта без пробелов. Пароль: 8–256 байт."};

    std::lock_guard<std::mutex> lock(mutex_);
    if (registration) {
        Salt salt{};
        if (RAND_bytes(salt.data(), salt.size()) != 1)
            throw std::runtime_error("Salt generation failed");
        const auto hash = hash_password(c.password, salt.data());
        auto stmt = prepare(db_, "INSERT INTO accounts (username, salt, password_hash) VALUES (?, ?, ?)");
        sqlite3_bind_text(stmt.get(), 1, c.username.data(), c.username.size(), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt.get(), 2, salt.data(), salt.size(), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt.get(), 3, hash.data(), hash.size(), SQLITE_TRANSIENT);
        const int result = sqlite3_step(stmt.get());
        if (result == SQLITE_CONSTRAINT) return {Status::NameTaken, 0, {}, "Этот логин уже занят."};
        if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
        return {Status::Ok, static_cast<uint32_t>(sqlite3_last_insert_rowid(db_)), c.username, {}};
    }
    auto stmt = prepare(db_, "SELECT id, salt, password_hash FROM accounts WHERE username = ?");
    sqlite3_bind_text(stmt.get(), 1, c.username.data(), c.username.size(), SQLITE_TRANSIENT);
    const int result = sqlite3_step(stmt.get());
    if (result == SQLITE_DONE) return {Status::WrongCredentials, 0, {}, "Неверный логин или пароль."};
    if (result != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
    if (sqlite3_column_bytes(stmt.get(), 1) != Salt{}.size() ||
        sqlite3_column_bytes(stmt.get(), 2) != Hash{}.size())
        throw std::runtime_error("Invalid account record");
    const auto salt = static_cast<const unsigned char*>(sqlite3_column_blob(stmt.get(), 1));
    const auto hash = hash_password(c.password, salt);
    if (CRYPTO_memcmp(hash.data(), sqlite3_column_blob(stmt.get(), 2), hash.size()) != 0)
        return {Status::WrongCredentials, 0, {}, "Неверный логин или пароль."};
    return {Status::Ok, static_cast<uint32_t>(sqlite3_column_int64(stmt.get(), 0)), c.username, {}};
}
