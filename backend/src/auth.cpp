#include "auth.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <utility>

namespace {
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void Check(int result) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW)
        throw std::runtime_error("Authentication database operation failed");
}

class Statement {
    sqlite3_stmt* stmt_ = nullptr;
public:
    Statement(sqlite3* db, const char* sql) { Check(sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr)); }
    ~Statement() { sqlite3_finalize(stmt_); }
    void Bind(int index, const std::string& value) {
        Check(sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void Bind(int index, std::int64_t value) { Check(sqlite3_bind_int64(stmt_, index, value)); }
    bool Next() { int result = sqlite3_step(stmt_); Check(result); return result == SQLITE_ROW; }
    std::int64_t Int(int column) { return sqlite3_column_int64(stmt_, column); }
    std::string Text(int column) {
        const auto* value = sqlite3_column_text(stmt_, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    }
};

std::string Hex(const unsigned char* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        result[i * 2] = digits[bytes[i] >> 4];
        result[i * 2 + 1] = digits[bytes[i] & 15];
    }
    return result;
}

std::string RandomHex(int bytes) {
    std::string buffer(bytes, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data()), bytes) != 1)
        throw std::runtime_error("Secure random generator failed");
    return Hex(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size());
}

std::string Digest(const std::string& token) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    if (!SHA256(reinterpret_cast<const unsigned char*>(token.data()), token.size(), digest.data()))
        throw std::runtime_error("Token hashing failed");
    return Hex(digest.data(), digest.size());
}

// Versioned format fixes the work factor; stored salts are independently random.
std::string PasswordHash(const std::string& password, const std::string& salt) {
    std::array<unsigned char, 32> hash{};
    if (EVP_PBE_scrypt(password.data(), password.size(),
        reinterpret_cast<const unsigned char*>(salt.data()), salt.size(),
        131072, 8, 1, 256ULL * 1024 * 1024, hash.data(), hash.size()) != 1)
        throw std::runtime_error("Password hashing failed");
    return "scrypt-v1:" + salt + ":" + Hex(hash.data(), hash.size());
}

bool VerifyPassword(const std::string& password, const std::string& stored) {
    if (password.size() > 1024 || stored.size() != 107 || stored.substr(0, 10) != "scrypt-v1:")
        return false;
    const auto candidate = PasswordHash(password, stored.substr(10, 32));
    return CRYPTO_memcmp(candidate.data(), stored.data(), stored.size()) == 0;
}

std::string Username(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return name;
}

void RequirePlayer(std::int64_t user_id) {
    if (user_id <= SERVER_USER_ID)
        throw ApiError(401, "AUTH_REQUIRED", "Служебный ID не может использоваться аккаунтом игрока.");
}

void RequirePlayer(const AuthContext& context) {
    if (!context.principal) throw ApiError(401, "AUTH_REQUIRED", "Войдите в аккаунт.");
    RequirePlayer(context.principal->user_id);
}

void RequireSession(const AuthContext& context) {
    RequirePlayer(context);
    if (!context.is_session)
        throw ApiError(403, "SESSION_REQUIRED", "Войдите с логином и паролем для управления API-ключами.");
}
}

ApiError::ApiError(int status, std::string code, const std::string& message)
    : std::runtime_error(message), status(status), code(std::move(code)) {}

AuthStore::AuthStore(const std::string& path, PrincipalRegistry& principals) : principals_(principals) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open authentication database");
    }
    try {
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        Check(sqlite3_busy_timeout(db_, 5000));
        const char* schema =
            "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;"
            "CREATE TABLE IF NOT EXISTS users("
            "id INTEGER PRIMARY KEY CHECK(id > 0), username TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS credentials("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL CHECK(user_id > 0) REFERENCES users(id),"
            "digest TEXT NOT NULL UNIQUE, kind TEXT NOT NULL CHECK(kind IN ('session','api')),"
            "name TEXT NOT NULL, prefix TEXT NOT NULL, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL);"
            "CREATE INDEX IF NOT EXISTS credentials_user ON credentials(user_id);"
            // CREATE TABLE IF NOT EXISTS does not add CHECKs to existing databases.
            // AFTER INSERT sees SQLite's allocated rowid, unlike BEFORE INSERT.
            "CREATE TRIGGER IF NOT EXISTS users_positive_id_insert AFTER INSERT ON users "
            "WHEN NEW.id <= 0 BEGIN SELECT RAISE(ABORT,'Player user_id must be positive'); END;"
            "CREATE TRIGGER IF NOT EXISTS users_positive_id_update BEFORE UPDATE ON users "
            "WHEN NEW.id <= 0 BEGIN SELECT RAISE(ABORT,'Player user_id must be positive'); END;"
            "CREATE TRIGGER IF NOT EXISTS credentials_positive_user_insert BEFORE INSERT ON credentials "
            "WHEN NEW.user_id <= 0 BEGIN SELECT RAISE(ABORT,'Credential user_id must be positive'); END;"
            "CREATE TRIGGER IF NOT EXISTS credentials_positive_user_update BEFORE UPDATE OF user_id ON credentials "
            "WHEN NEW.user_id <= 0 BEGIN SELECT RAISE(ABORT,'Credential user_id must be positive'); END;";
        Check(sqlite3_exec(db_, schema, nullptr, nullptr, nullptr));
        Statement reserved(db_, "SELECT id FROM users WHERE id <= 0 LIMIT 1");
        if (reserved.Next())
            throw std::runtime_error("Authentication database contains a player with user_id <= 0. "
                "ID 0 is reserved for the server; migrate this account before startup.");
        Statement players(db_, "SELECT id,username FROM users");
        while (players.Next()) principals_.Add(players.Int(0), players.Text(1));
        dummy_password_ = PasswordHash(RandomHex(32), RandomHex(16));
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}

AuthStore::~AuthStore() { sqlite3_close(db_); }

// Caller holds mutex_. No password derivation runs under this mutex.
std::string AuthStore::IssueSession(std::int64_t user_id) {
    RequirePlayer(user_id);
    const auto now = Now();
    Statement cleanup(db_, "DELETE FROM credentials WHERE expires_at<=? OR "
        "(kind='session' AND user_id=? AND id NOT IN "
        "(SELECT id FROM credentials WHERE kind='session' AND user_id=? ORDER BY id DESC LIMIT 9))");
    cleanup.Bind(1, now); cleanup.Bind(2, user_id); cleanup.Bind(3, user_id); cleanup.Next();
    const auto token = "ast_session_" + RandomHex(32);
    Statement insert(db_, "INSERT INTO credentials(user_id,digest,kind,name,prefix,created_at,expires_at) "
        "VALUES(?,?,'session','','',?,?)");
    insert.Bind(1, user_id); insert.Bind(2, Digest(token)); insert.Bind(3, now); insert.Bind(4, now + 86400);
    insert.Next();
    return token;
}

std::string AuthStore::Register(const std::string& username, const std::string& password) {
    const auto name = Username(username);
    if (name.size() < 3 || name.size() > 32 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
        throw ApiError(400, "INVALID_USERNAME", "Логин: от 3 до 32 латинских букв, цифр, символов _ или -.");
    if (password.size() < 12 || password.size() > 1024)
        throw ApiError(400, "INVALID_PASSWORD", "Пароль: от 12 до 1024 байт.");
    const auto hash = PasswordHash(password, RandomHex(16));
    std::lock_guard<std::mutex> lock(mutex_);
    Statement existing(db_, "SELECT id FROM users WHERE username=?");
    existing.Bind(1, name);
    if (existing.Next()) throw ApiError(409, "USERNAME_TAKEN", "Этот логин уже занят.");
    Check(sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr));
    std::int64_t user_id = 0;
    std::string token;
    try {
        Statement insert(db_, "INSERT INTO users(username,password_hash) VALUES(?,?)");
        insert.Bind(1, name); insert.Bind(2, hash); insert.Next();
        user_id = sqlite3_last_insert_rowid(db_);
        token = IssueSession(user_id);
        Check(sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr));
    } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
    principals_.Add(user_id, name);
    return token;
}

std::string AuthStore::Login(const std::string& username, const std::string& password) {
    std::int64_t user_id = 0;
    std::string stored = dummy_password_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Statement select(db_, "SELECT id,password_hash FROM users WHERE username=?");
        select.Bind(1, Username(username));
        if (select.Next()) { user_id = select.Int(0); stored = select.Text(1); }
    }
    const bool valid = VerifyPassword(password, stored);
    if (!valid || user_id <= SERVER_USER_ID) throw ApiError(401, "INVALID_CREDENTIALS", "Неверный логин или пароль.");
    std::lock_guard<std::mutex> lock(mutex_);
    return IssueSession(user_id);
}

std::optional<AuthContext> AuthStore::Authenticate(const std::string& token) {
    if (token.empty() || token.size() > 128) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(db_, "SELECT c.user_id,c.id,c.kind,u.username FROM credentials c "
        "JOIN users u ON u.id=c.user_id WHERE c.digest=? AND c.expires_at>? AND c.user_id>0");
    select.Bind(1, Digest(token)); select.Bind(2, Now());
    if (!select.Next()) return std::nullopt;
    return AuthContext{principals_.Add(select.Int(0), select.Text(3)), select.Int(1), select.Text(2) == "session"};
}

boost::json::value AuthStore::Profile(const AuthContext& context) {
    RequirePlayer(context);
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(db_, "SELECT username FROM users WHERE id=?");
    select.Bind(1, context.principal->user_id);
    if (!select.Next()) throw ApiError(401, "AUTH_REQUIRED", "Войдите в аккаунт.");
    return boost::json::object{{"id", context.principal->user_id}, {"username", select.Text(0)},
        {"credential", context.is_session ? "session" : "api_key"}};
}

void AuthStore::Logout(const AuthContext& context) {
    RequireSession(context);
    std::lock_guard<std::mutex> lock(mutex_);
    Statement remove(db_, "DELETE FROM credentials WHERE id=?");
    remove.Bind(1, context.credential_id); remove.Next();
}

boost::json::value AuthStore::CreateKey(const AuthContext& context, const std::string& name,
                                       int days, const std::string& password) {
    RequireSession(context);
    if (name.empty() || name.size() > 80 || days < 1 || days > 365)
        throw ApiError(400, "INVALID_KEY", "Задайте название ключа и срок от 1 до 365 дней.");
    std::string stored;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Statement select(db_, "SELECT password_hash FROM users WHERE id=?");
        select.Bind(1, context.principal->user_id);
        if (!select.Next()) throw ApiError(401, "AUTH_REQUIRED", "Войдите в аккаунт.");
        stored = select.Text(0);
    }
    if (!VerifyPassword(password, stored)) throw ApiError(401, "INVALID_CREDENTIALS", "Неверный пароль.");
    const auto now = Now(), expires = now + static_cast<std::int64_t>(days) * 86400;
    const auto token = "ast_key_" + RandomHex(32);
    std::lock_guard<std::mutex> lock(mutex_);
    Statement count(db_, "SELECT COUNT(*) FROM credentials WHERE user_id=? AND kind='api' AND expires_at>?");
    count.Bind(1, context.principal->user_id); count.Bind(2, now); count.Next();
    if (count.Int(0) >= 20) throw ApiError(409, "KEY_LIMIT", "Сначала отзовите один из 20 действующих ключей.");
    Statement insert(db_, "INSERT INTO credentials(user_id,digest,kind,name,prefix,created_at,expires_at) VALUES(?,?,'api',?,?,?,?)");
    insert.Bind(1, context.principal->user_id); insert.Bind(2, Digest(token)); insert.Bind(3, name);
    insert.Bind(4, token.substr(0, 16)); insert.Bind(5, now); insert.Bind(6, expires); insert.Next();
    return boost::json::object{{"id", sqlite3_last_insert_rowid(db_)}, {"key", token},
        {"name", name}, {"expires_at", expires}, {"scope", "game:read"}};
}

boost::json::value AuthStore::ListKeys(const AuthContext& context) {
    RequireSession(context);
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(db_, "SELECT id,name,prefix,created_at,expires_at FROM credentials WHERE user_id=? AND kind='api' ORDER BY id DESC");
    select.Bind(1, context.principal->user_id);
    boost::json::array result;
    while (select.Next()) result.push_back(boost::json::object{{"id", select.Int(0)}, {"name", select.Text(1)},
        {"prefix", select.Text(2)}, {"created_at", select.Int(3)}, {"expires_at", select.Int(4)}, {"scope", "game:read"}});
    return result;
}

void AuthStore::RevokeKey(const AuthContext& context, std::int64_t id) {
    RequireSession(context);
    std::lock_guard<std::mutex> lock(mutex_);
    Statement remove(db_, "DELETE FROM credentials WHERE id=? AND user_id=? AND kind='api'");
    remove.Bind(1, id); remove.Bind(2, context.principal->user_id); remove.Next();
    if (!sqlite3_changes(db_)) throw ApiError(404, "NOT_FOUND", "Ключ не найден.");
}
