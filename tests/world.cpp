#include "game_server.hpp"
#include "protocol.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

float f32(protocol::Reader& reader) {
    const auto bits = reader.u32();
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
int main() {
    World world;
    Connection first(world), second(world), duplicate(world);
    assert(first.Login(1, "first"));
    assert(second.Login(2, "second"));
    assert(!duplicate.Login(1, "first"));
    assert(world.crafts.size() == 2);
    assert(world.crafts[0].GetOwner()->id == 1);
    world.crafts[0].SetVelocity({2, -1});
    const auto start = world.crafts[0].GetPosition();
    UpdateGameServer(&world, 0.5f);
    assert(world.crafts[0].GetPosition() == start + Vector2(1, -0.5f));
    const auto bytes = EncodeWorld(world);
    const std::string packet(bytes.begin(), bytes.end());
    protocol::Reader reader(packet);
    assert(reader.u8() == 130);
    assert(reader.u32() == 2);
    for (const auto& craft : world.crafts) {
        assert(reader.u32() == craft.GetId());
        assert(reader.u32() == craft.GetOwner()->id);
        assert(reader.string() == craft.GetOwner()->username);
        assert(f32(reader) == craft.GetPosition().x);
        assert(f32(reader) == craft.GetPosition().y);
        assert(f32(reader) == craft.GetVelocity().x);
        assert(f32(reader) == craft.GetVelocity().y);
        assert(reader.u32() == 0 && reader.u8() == 0);
    }
    reader.finish();
    first.Logout();
    assert(world.crafts.size() == 2);
    assert(duplicate.Login(1, "first"));
    assert(world.crafts.size() == 2);
    std::cout << "World tests passed\n";
}
