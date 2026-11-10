#include "game_server.hpp"
#include "protocol.hpp"

#include <iostream>

World* StartGameServer() {
    std::cout << "Game server started" << std::endl;
    return new World();
}

void World::EnsureCraft(Player& player) {
    for (const auto& craft : crafts)
        if (craft.GetOwner() == &player) return;
    const auto slot = player.id - 1;
    crafts.emplace_back(&player, next_craft_id_++, Vector2{
        float(slot % 16) * 80.0f - 600.0f, float(slot / 16) * 80.0f - 300.0f});
}

std::optional<std::string> HandleScriptMessage(World&, Craft&, const std::string&) {
    // Implement your application protocol here. Replies go through craft.script->Send().
    return std::nullopt;
}

void UpdateGameServer(World* world, float delta_time) {
    std::lock_guard<std::mutex> lock(world->mutex);
    for (auto& entity : world->entities) entity.Update(delta_time);
    for (auto& craft : world->crafts) {
        craft.Update(delta_time);
        if (!craft.script) continue;
        for (const auto& message : craft.script->TakeMessages()) {
            try {
                if (auto reply = HandleScriptMessage(*world, craft, message))
                    craft.script->Send(*reply);
            } catch (const std::exception& error) {
                std::cerr << "Script handler: " << error.what() << '\n';
                craft.script->Stop();
            }
        }
    }
}

void Craft::Update(float delta_time) {
    SetPosition(GetPosition() + GetVelocity() * delta_time);
}

std::vector<uint8_t> EncodeWorld(World& world) {
    std::lock_guard<std::mutex> lock(world.mutex);
    protocol::Writer out;
    out.u8(static_cast<uint8_t>(protocol::Response::World));
    out.u32(static_cast<uint32_t>(world.crafts.size()));
    for (const auto& craft : world.crafts) {
        const auto* owner = craft.GetOwner();
        out.u32(static_cast<uint32_t>(craft.GetId()));
        out.u32(owner ? owner->id : 0);
        out.string(owner ? owner->username : "");
        out.f32(craft.GetPosition().x); out.f32(craft.GetPosition().y);
        out.f32(craft.GetVelocity().x); out.f32(craft.GetVelocity().y);
        out.u32(static_cast<uint32_t>(craft.GetModules().size()));
        out.u8(static_cast<uint8_t>(craft.script ? craft.script->state() : ScriptRuntime::State::None));
    }
    return out.bytes;
}
