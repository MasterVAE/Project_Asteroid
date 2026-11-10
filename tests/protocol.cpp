#include "protocol.hpp"

#include <cassert>
#include <iostream>

namespace {
std::string packet(const protocol::Writer& writer) {
    return {writer.bytes.begin(), writer.bytes.end()};
}

template<class Exception, class Action>
void rejects(Action action) {
    bool rejected = false;
    try { action(); }
    catch (const Exception&) { rejected = true; }
    assert(rejected);
}

void round_trip(const std::string& value) {
    protocol::Writer writer;
    writer.u8(42);
    writer.long_string(value);
    writer.string("tail");
    assert(writer.bytes.size() == 1 + 4 + value.size() + 2 + 4);
    for (int i = 0; i < 4; ++i)
        assert(writer.bytes[1 + i] == ((value.size() >> (8 * i)) & 255));
    const auto bytes = packet(writer);
    protocol::Reader reader(bytes);
    assert(reader.u8() == 42);
    assert(reader.long_string() == value);
    assert(reader.string() == "tail");
    reader.finish();
}
}

int main() {
    round_trip("");
    round_trip(std::string("a\0b", 3));
    for (const size_t size : {65535u, 65536u, protocol::MaxLongStringBytes})
        round_trip(std::string(size, 'x'));
    std::string unicode;
    for (size_t i = 0; i < protocol::MaxLongStringBytes / 2; ++i) unicode += u8"я";
    round_trip(unicode);

    protocol::Writer writer;
    writer.u8(42);
    rejects<std::length_error>([&] {
        writer.long_string(std::string(protocol::MaxLongStringBytes + 1, 'x'));
    });
    assert(writer.bytes == std::vector<uint8_t>{42});
    rejects<std::length_error>([&] { writer.string(std::string(65536, 'x')); });

    for (const uint32_t size : {protocol::MaxLongStringBytes + 1, UINT32_MAX}) {
        protocol::Writer header;
        header.u32(size);
        const auto bytes = packet(header);
        protocol::Reader reader(bytes);
        rejects<std::invalid_argument>([&] { reader.long_string(); });
    }
    for (size_t size = 0; size < 7; ++size) {
        // Declares three payload bytes; every shorter prefix must fail.
        const auto bytes = std::string("\x03\0\0\0abc", 7).substr(0, size);
        protocol::Reader reader(bytes);
        rejects<std::invalid_argument>([&] { reader.long_string(); });
    }
    const std::string trailing(5, '\0');
    protocol::Reader reader(trailing);
    assert(reader.long_string().empty());
    rejects<std::invalid_argument>([&] { reader.finish(); });

    protocol::Writer legacy;
    legacy.string("abc");
    assert((legacy.bytes == std::vector<uint8_t>{3, 0, 'a', 'b', 'c'}));
    std::cout << "C++ protocol tests passed\n";
}
