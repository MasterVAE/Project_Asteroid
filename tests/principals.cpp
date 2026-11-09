#include "api.hpp"
#include "auth.hpp"
#include "principal.hpp"
#include "world.hpp"

#include <boost/json/object.hpp>
#include <boost/json/array.hpp>
#include <boost/json/value.hpp>
#include <sqlite3.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>
#include <unistd.h>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Directory {
    std::string path = (std::filesystem::temp_directory_path() / "asteroid-principals-XXXXXX").string();
    Directory() { if (!mkdtemp(path.data())) throw std::runtime_error("Cannot create test directory"); }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void GrowDatabase(const std::string& path) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.c_str(), &database) != SQLITE_OK) throw std::runtime_error("Cannot open fixture database");
    std::string sql = "BEGIN;";
    for (int id = 3; id <= 512; ++id)
        sql += "INSERT INTO users(id,username,password_hash) VALUES(" + std::to_string(id) +
            ",'fixture_" + std::to_string(id) + "','unused');";
    sql += "COMMIT;";
    const int result = sqlite3_exec(database, sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(database);
    Expect(result == SQLITE_OK, "Cannot grow fixture database");
}
}

int main() {
    alarm(30); // Fail promptly if lock ordering regresses.
    static_assert(!std::is_copy_constructible_v<Principal>);
    static_assert(!std::is_move_constructible_v<Principal>);
    static_assert(!std::is_copy_constructible_v<PrincipalRegistry>);
    try {
        Directory directory;
        const auto path = directory.path + "/auth.sqlite3";
        const std::string password = "test-only-password-123";
        World world;
        const auto* server = Principal::Server();
        Expect(server->user_id == SERVER_USER_ID, "Wrong server ID");
        Expect(world.GetPrincipals().Find(0) == server, "Server identity is not canonical");
        for (const auto& entry : world.GetEntities()) Expect(entry.second->owner == server, "Default owner is not server");
        const Principal* owner = nullptr;
        Entity* owned_entity = nullptr;
        std::string active_session;
        {
            AuthStore auth(path, world.GetPrincipals());
            const auto first_token = auth.Register("alice", password);
            auto first = auth.Authenticate(first_token);
            Expect(first.has_value(), "Registration did not authenticate");
            owner = first->principal;
            Expect(owner->user_id == 1 && owner->username == "alice", "Wrong identity data");
            Expect(auth.GetPrincipal(1) == owner, "ID lookup returned a different identity");
            Expect(auth.GetPrincipal(999) == nullptr, "Missing account has an identity");
            const auto id = world.CreateEntity(owner);
            owned_entity = world.GetEntities().at(id);
            Expect(owned_entity->owner == owner, "Entity did not retain the canonical pointer");
            first.reset();
            Expect(owned_entity->owner->user_id == 1, "Temporary context owned the identity");

            active_session = auth.Login("alice", password);
            const auto second = auth.Authenticate(active_session);
            const auto first_again = auth.Authenticate(first_token);
            Expect(second->principal == owner && first_again->principal == owner, "Sessions returned different identities");
            Expect(second->credential_id != first_again->credential_id, "Credential contexts were merged");
            const auto key = auth.CreateKey(*second, "test-only", 1, password).as_object();
            const std::string token(key.at("key").as_string());
            const auto key_context = auth.Authenticate(token);
            Expect(key_context->principal == owner && !key_context->is_session && second->is_session,
                "Key and session identity/context separation failed");

            std::atomic<bool> stable{true};
            std::vector<std::thread> readers;
            for (int thread = 0; thread < 4; ++thread) readers.emplace_back([&] {
                for (int i = 0; i < 64; ++i) {
                    const auto context = auth.Authenticate(i % 2 ? token : active_session);
                    if (!context || context->principal != owner) stable.store(false);
                }
            });
            for (auto& reader : readers) reader.join();
            Expect(stable.load(), "Concurrent authentication returned different identities");

            ApiService api(auth, world);
            const std::string mine_request = "{\"type\":\"request\",\"request_type\":\"get_my_entities\"}";
            const auto anonymous = api.Handle(mine_request, "", "test", true);
            Expect(anonymous.status == 401, "Anonymous request received owned entities");
            const auto mine = api.Handle(mine_request, active_session, "test", true);
            const auto& own_entities = mine.body.as_object().at("result").as_object().at("entities").as_array();
            Expect(mine.status == 200 && own_entities.size() == 1, "Owned entity was not returned");
            Expect(own_entities.front().as_object().at("owner_id").as_int64() == owner->user_id,
                "Owned entity has the wrong owner");
            const auto key_mine = api.Handle(mine_request, token, "test", false);
            Expect(key_mine.status == 200 && key_mine.body == mine.body, "Key/session transports return different owned entities");
            const auto bob_token = auth.Register("bob", password);
            const auto bob = auth.Authenticate(bob_token);
            const auto empty = api.Handle(mine_request, bob_token, "test", false);
            Expect(empty.body.as_object().at("result").as_object().at("entities").as_array().empty(),
                "New player sees another player's ships");
            world.CreateEntity(bob->principal);
            const auto spoof = api.Handle("{\"type\":\"request\",\"request_type\":\"get_my_entities\",\"owner_id\":2}", active_session, "test", false);
            Expect(spoof.body == mine.body, "Client-supplied owner_id changed ownership filtering");
            const auto rejected = api.Handle("{\"type\":\"request\",\"request_type\":\"create_craft\"}", token, "test", true);
            Expect(rejected.status == 403, "Read-only key created a ship");
            const auto created = api.Handle("{\"type\":\"request\",\"request_type\":\"create_craft\"}", active_session, "test", true);
            Expect(created.status == 200, "Session could not create a ship");
            const auto& entity = created.body.as_object().at("result").as_object();
            Expect(entity.at("owner_id").as_int64() == owner->user_id, "API returned the wrong owner");
            Expect(world.GetEntities().at(entity.at("id").as_uint64())->owner == owner, "API ship has a temporary owner");
            const auto refreshed = api.Handle(mine_request, active_session, "test", false);
            const auto& fleet = refreshed.body.as_object().at("result").as_object().at("entities").as_array();
            Expect(fleet.size() == 2, "Newly created ship is missing from owned entities");
            for (const auto& ship : fleet) Expect(ship.as_object().at("owner_id").as_int64() == owner->user_id,
                "Foreign/server ship leaked into owned entities");

            auth.Logout(*first_again);
            auth.RevokeKey(*second, key.at("id").as_int64());
            Expect(!auth.Authenticate(first_token) && !auth.Authenticate(token), "Revocation failed");
            Expect(owned_entity->owner == owner && auth.GetPrincipal(1) == owner, "Revocation invalidated owner");
        }
        Expect(owned_entity->owner == owner && owner->user_id == 1, "AuthStore destruction invalidated owner");
        GrowDatabase(path);
        {
            AuthStore reloaded(path, world.GetPrincipals());
            Expect(reloaded.Authenticate(active_session)->principal == owner, "Reload replaced an existing identity");
            Expect(reloaded.GetPrincipal(512) != nullptr, "Existing offline accounts were not loaded");
            Expect(owned_entity->owner == owner, "Registry growth/rehash invalidated owner");
            Expect(reloaded.GetPrincipal(1) != reloaded.GetPrincipal(512), "Different accounts share an identity");
        }
        {
            World other_world;
            AuthStore other_auth(path, other_world.GetPrincipals());
            bool rejected = false;
            try { world.CreateEntity(other_auth.GetPrincipal(1)); }
            catch (const std::invalid_argument&) { rejected = true; }
            Expect(rejected, "Entity accepted an identity from a shorter-lived foreign registry");
        }
        bool rejected = false;
        try { world.CreateEntity(nullptr); }
        catch (const std::invalid_argument&) { rejected = true; }
        Expect(rejected, "Entity accepted a null owner");
        Expect(owned_entity->owner == owner && owner->username == "alice", "Owner did not survive to world lifetime");
        std::cout << "PASS: canonical immutable identities, concurrent sessions/keys, owner lifetime, reload/rehash and API ownership\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
