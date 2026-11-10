#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Keep these layouts in sync with frontend/browser/protocol.js.
// Integers are little-endian; strings are [u16 byte length][UTF-8 bytes].
namespace protocol {
enum class Request : uint8_t { Login = 1, Register = 2, Logout = 3 };
enum class Response : uint8_t { Auth = 128, Logout = 129, Error = 255 };
enum class Status : uint8_t {
    Ok = 0, InvalidInput = 1, NameTaken = 2, WrongCredentials = 3, ServerError = 4
};

struct Credentials { std::string username; std::string password; };
struct AuthResponse {
    Status status = Status::Ok;
    uint32_t user_id = 0;
    std::string username;
    std::string message;
};

class Reader {
public:
    explicit Reader(const std::string& bytes) : bytes_(bytes) {}
    uint8_t u8() {
        require(1);
        return static_cast<uint8_t>(bytes_[offset_++]);
    }
    uint16_t u16() {
        const auto low = u8();
        return low | (uint16_t(u8()) << 8);
    }
    std::string string() {
        const auto size = u16();
        require(size);
        auto value = bytes_.substr(offset_, size);
        offset_ += size;
        return value;
    }
    Credentials credentials() { return {string(), string()}; }
    void finish() const {
        if (offset_ != bytes_.size()) throw std::invalid_argument("Лишние данные в пакете");
    }
private:
    void require(size_t size) const {
        if (size > bytes_.size() - offset_) throw std::invalid_argument("Неполный пакет");
    }
    const std::string& bytes_;
    size_t offset_ = 0;
};

class Writer {
public:
    std::vector<uint8_t> bytes;
    void u8(uint8_t value) { bytes.push_back(value); }
    void u16(uint16_t value) { u8(value & 255); u8(value >> 8); }
    void u32(uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) u8((value >> shift) & 255);
    }
    void string(const std::string& value) {
        if (value.size() > 65535) throw std::length_error("Строка слишком длинная");
        u16(static_cast<uint16_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};

// [128][status:u8][user_id:u32][username:string][message:string]
inline std::vector<uint8_t> encode(const AuthResponse& response) {
    Writer out;
    out.u8(static_cast<uint8_t>(Response::Auth));
    out.u8(static_cast<uint8_t>(response.status));
    out.u32(response.user_id);
    out.string(response.username);
    out.string(response.message);
    return out.bytes;
}
inline std::vector<uint8_t> error(const std::string& message) {
    Writer out;
    out.u8(static_cast<uint8_t>(Response::Error));
    out.string(message);
    return out.bytes;
}
} // namespace protocol
