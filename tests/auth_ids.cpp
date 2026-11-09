#include "auth.hpp"

#include <boost/json/object.hpp>
#include <openssl/sha.h>
#include <sqlite3.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Directory {
    std::string path = (std::filesystem::temp_directory_path() / "asteroid-auth-ids-XXXXXX").string();
    Directory() { if (!mkdtemp(path.data())) throw std::runtime_error("Cannot create test directory"); }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

struct Database {
    sqlite3* db = nullptr;
    explicit Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) throw std::runtime_error("Cannot open test database");
        sqlite3_busy_timeout(db, 5000);
    }
    ~Database() { sqlite3_close(db); }
    void Exec(const std::string& sql) {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    void Reject(const std::string& sql) {
        Expect((sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) & 0xff) == SQLITE_CONSTRAINT,
            "Reserved ID was accepted by the database");
    }
    int Count(const char* sql) {
        sqlite3_stmt* statement = nullptr;
        Expect(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK, "Cannot prepare query");
        const int step = sqlite3_step(statement);
        const int result = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
        Expect(step == SQLITE_ROW, "Cannot read query");
        return result;
    }
};

std::string Digest(const std::string& token) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> bytes{};
    SHA256(reinterpret_cast<const unsigned char*>(token.data()), token.size(), bytes.data());
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (auto byte : bytes) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
    return hex;
}

void CheckDatabaseGuards(Database& database) {
    for (const auto id : {"0", "-1"}) {
        database.Reject(std::string("INSERT INTO users(id,username,password_hash) VALUES(") + id + ",'reserved','unused')");
        for (const auto column : {"id", "rowid", "oid", "_rowid_"}) {
            database.Reject(std::string("UPDATE users SET ") + column + "=" + id + " WHERE id=1");
            database.Reject(std::string("INSERT INTO users(") + column + ",username,password_hash) VALUES(" +
                id + ",'reserved_alias','unused')");
        }
        database.Reject(std::string("INSERT INTO credentials(user_id,digest,kind,name,prefix,created_at,expires_at) VALUES(") +
            id + ",'invalid','session','','',0,4102444800)");
        database.Reject(std::string("UPDATE credentials SET user_id=") + id + " WHERE user_id=1");
    }
    Expect(database.Count("SELECT COUNT(*) FROM users WHERE id<=0") == 0, "Invalid user persisted");
    Expect(database.Count("SELECT COUNT(*) FROM users WHERE id=1") == 1, "Existing account changed");
}

constexpr char LegacySchema[] =
    "CREATE TABLE users(id INTEGER PRIMARY KEY,username TEXT NOT NULL UNIQUE,password_hash TEXT NOT NULL);"
    "CREATE TABLE credentials(id INTEGER PRIMARY KEY AUTOINCREMENT,user_id INTEGER NOT NULL REFERENCES users(id),"
    "digest TEXT NOT NULL UNIQUE,kind TEXT NOT NULL,name TEXT NOT NULL,prefix TEXT NOT NULL,"
    "created_at INTEGER NOT NULL,expires_at INTEGER NOT NULL);";
}

int main() {
    try {
        static_assert(SERVER_USER_ID == 0);
        Directory directory;
        const auto fresh_path = directory.path + "/fresh.sqlite3";
        {
            PrincipalRegistry principals;
            AuthStore auth(fresh_path, principals);
            const std::string password = "test-only-password-123";
            const auto token = auth.Register("alice", password);
            const auto player = auth.Authenticate(token);
            Expect(player && player->principal->user_id == 1, "First account must receive ID 1");
            Expect(auth.Profile(*player).as_object().at("id").as_int64() == 1, "Incorrect profile ID");
            Expect(auth.Authenticate(auth.Login("alice", password))->principal->user_id == 1, "Login changed account ID");
            Database database(fresh_path);
            CheckDatabaseGuards(database);
            try {
                auth.Logout(AuthContext{Principal::Server(), player->credential_id, true});
                throw std::runtime_error("Server principal was accepted as a player");
            } catch (const ApiError& error) { Expect(error.status == 401, "Wrong rejection status"); }
            Expect(auth.Authenticate(token).has_value(), "Rejected server principal revoked a player session");
        }
        const auto legacy_path = directory.path + "/legacy.sqlite3";
        {
            Database database(legacy_path);
            database.Exec(LegacySchema);
            database.Exec("INSERT INTO users VALUES(1,'legacy','unused')");
            database.Exec("INSERT INTO credentials(user_id,digest,kind,name,prefix,created_at,expires_at) "
                "VALUES(1,'existing','session','','',0,4102444800)");
            // Simulate a pre-existing invalid credential; it must never authenticate.
            database.Exec("INSERT INTO credentials(user_id,digest,kind,name,prefix,created_at,expires_at) "
                "VALUES(0,'" + Digest("synthetic-server-token") + "','session','','',0,4102444800)");
        }
        {
            PrincipalRegistry principals;
            AuthStore auth(legacy_path, principals);
            Database database(legacy_path);
            CheckDatabaseGuards(database);
            Expect(!auth.Authenticate("synthetic-server-token"), "Legacy user_id 0 authenticated");
            try {
                auth.Profile(AuthContext{Principal::Server(), 1, true});
                throw std::runtime_error("Server ID has a player profile");
            } catch (const ApiError& error) { Expect(error.status == 401, "Wrong profile rejection status"); }
        }
        for (const auto id : {"0", "-1"}) {
            const auto path = directory.path + "/invalid" + id + ".sqlite3";
            Database database(path);
            database.Exec(LegacySchema);
            database.Exec(std::string("INSERT INTO users VALUES(") + id + ",'legacy','unused')");
            bool refused = false;
            PrincipalRegistry principals;
            try { AuthStore auth(path, principals); }
            catch (const std::runtime_error& error) {
                refused = std::string(error.what()).find("ID 0 is reserved") != std::string::npos;
            }
            Expect(refused, "Startup accepted an existing reserved player ID");
            Expect(database.Count("SELECT COUNT(*) FROM users") == 1, "Startup removed an existing account");
        }
        std::cout << "PASS: positive registration/login IDs, new and legacy DB guards, reserved credentials, startup validation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
