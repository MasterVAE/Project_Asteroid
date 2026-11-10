
#pragma once

#include <cmath>
#include <ostream>

struct Vector2
{
    float x = 0.0f;
    float y = 0.0f;

    // Constructors
    constexpr Vector2() = default;
    constexpr Vector2(float x, float y) : x(x), y(y) {}
    constexpr explicit Vector2(float value) : x(value), y(value) {}

    // Arithmetic operators
    constexpr Vector2 operator+(const Vector2& v) const {
        return {x + v.x, y + v.y};
    }

    constexpr Vector2 operator-(const Vector2& v) const {
        return {x - v.x, y - v.y};
    }

    constexpr Vector2 operator-() const {
        return {-x, -y};
    }

    constexpr Vector2 operator*(float scalar) const {
        return {x * scalar, y * scalar};
    }

    constexpr Vector2 operator/(float scalar) const {
        return {x / scalar, y / scalar};
    }

    // Component-wise operations
    constexpr Vector2 operator*(const Vector2& v) const {
        return {x * v.x, y * v.y};
    }

    constexpr Vector2 operator/(const Vector2& v) const {
        return {x / v.x, y / v.y};
    }

    // Assignment operators
    constexpr Vector2& operator+=(const Vector2& v) {
        x += v.x;
        y += v.y;
        return *this;
    }

    constexpr Vector2& operator-=(const Vector2& v) {
        x -= v.x;
        y -= v.y;
        return *this;
    }

    constexpr Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr Vector2& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    constexpr Vector2& operator*=(const Vector2& v) {
        x *= v.x;
        y *= v.y;
        return *this;
    }

    constexpr Vector2& operator/=(const Vector2& v) {
        x /= v.x;
        y /= v.y;
        return *this;
    }

    // Comparison operators
    constexpr bool operator==(const Vector2& v) const {
        return x == v.x && y == v.y;
    }

    constexpr bool operator!=(const Vector2& v) const {
        return !(*this == v);
    }

    // Indexing
    float& operator[](int index) {
        return index == 0 ? x : y;
    }

    const float& operator[](int index) const {
        return index == 0 ? x : y;
    }

    // Vector properties
    constexpr float magnitudeSquared() const {
        return x * x + y * y;
    }

    float magnitude() const {
        return std::sqrt(magnitudeSquared());
    }

    float length() const {
        return magnitude();
    }

    constexpr float lengthSquared() const {
        return magnitudeSquared();
    }

    // Normalization
    Vector2 normalized() const {
        float len = magnitude();
        if (len == 0.0f)
            return Zero();

        return *this / len;
    }

    void normalize() {
        float len = magnitude();
        if (len != 0.0f) {
            x /= len;
            y /= len;
        }
    }

    // Dot and cross products
    constexpr float dot(const Vector2& v) const {
        return x * v.x + y * v.y;
    }

    constexpr float cross(const Vector2& v) const {
        return x * v.y - y * v.x;
    }

    // Distance
    float distance(const Vector2& v) const {
        return (*this - v).magnitude();
    }

    constexpr float distanceSquared(const Vector2& v) const {
        return (*this - v).magnitudeSquared();
    }

    // Angles (radians)
    float angle() const {
        return std::atan2(y, x);
    }

    float angleTo(const Vector2& v) const {
        return std::atan2(cross(v), dot(v));
    }

    // Rotation (radians)
    Vector2 rotated(float radians) const {
        float c = std::cos(radians);
        float s = std::sin(radians);

        return {
            x * c - y * s,
            x * s + y * c
        };
    }

    void rotate(float radians) {
        *this = rotated(radians);
    }

    // Perpendicular vectors
    constexpr Vector2 perpendicular() const {
        return {-y, x};
    }

    constexpr Vector2 perpendicularCW() const {
        return {y, -x};
    }

    // Linear interpolation
    constexpr Vector2 lerp(const Vector2& target, float t) const {
        return *this + (target - *this) * t;
    }

    // Projection onto another vector
    Vector2 projectOnto(const Vector2& v) const {
        float lenSq = v.magnitudeSquared();

        if (lenSq == 0.0f)
            return Zero();

        return v * (dot(v) / lenSq);
    }

    // Reflection around a normal
    constexpr Vector2 reflect(const Vector2& normal) const {
        return *this - normal * (2.0f * dot(normal));
    }

    // Limit vector length
    Vector2 clampMagnitude(float maxLength) const {
        if (maxLength <= 0.0f)
            return Zero();

        float lenSq = magnitudeSquared();

        if (lenSq <= maxLength * maxLength)
            return *this;

        return normalized() * maxLength;
    }

    // Utilities
    constexpr bool isZero() const {
        return x == 0.0f && y == 0.0f;
    }

    bool isNearlyZero(float epsilon = 1e-6f) const {
        return std::fabs(x) <= epsilon &&
               std::fabs(y) <= epsilon;
    }

    bool nearlyEquals(const Vector2& v, float epsilon = 1e-6f) const {
        return std::fabs(x - v.x) <= epsilon &&
               std::fabs(y - v.y) <= epsilon;
    }

    // Common vectors
    static constexpr Vector2 Zero() {
        return {0.0f, 0.0f};
    }

    static constexpr Vector2 One() {
        return {1.0f, 1.0f};
    }

    static constexpr Vector2 Up() {
        return {0.0f, 1.0f};
    }

    static constexpr Vector2 Down() {
        return {0.0f, -1.0f};
    }

    static constexpr Vector2 Left() {
        return {-1.0f, 0.0f};
    }

    static constexpr Vector2 Right() {
        return {1.0f, 0.0f};
    }

    // Scalar * Vector
    friend constexpr Vector2 operator*(float scalar, const Vector2& v) {
        return v * scalar;
    }

    // Output
    friend std::ostream& operator<<(std::ostream& os, const Vector2& v) {
        return os << "Vector2(" << v.x << ", " << v.y << ")";
    }
};
