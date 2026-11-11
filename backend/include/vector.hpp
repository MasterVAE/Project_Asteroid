
#pragma once

#include <cmath>
#include <ostream>

class Vector2
{
public:
    float x = 0.0f;
    float y = 0.0f;

    // Constructors
    constexpr Vector2() = default;

    constexpr Vector2(float x, float y)
        : x(x), y(y) {}

    explicit constexpr Vector2(float value)
        : x(value), y(value) {}

    // Constants
    static constexpr Vector2 Zero()  { return {0.0f, 0.0f}; }
    static constexpr Vector2 One()   { return {1.0f, 1.0f}; }
    static constexpr Vector2 Up()    { return {0.0f, 1.0f}; }
    static constexpr Vector2 Down()  { return {0.0f, -1.0f}; }
    static constexpr Vector2 Left()  { return {-1.0f, 0.0f}; }
    static constexpr Vector2 Right() { return {1.0f, 0.0f}; }

    // Length
    float Length() const
    {
        return std::sqrt(LengthSquared());
    }

    constexpr float LengthSquared() const
    {
        return x * x + y * y;
    }

    // Normalization
    Vector2 Normalized() const
    {
        float length = Length();

        if (length <= 0.0f)
            return Zero();

        return *this / length;
    }

    void Normalize()
    {
        float length = Length();

        if (length > 0.0f)
        {
            x /= length;
            y /= length;
        }
    }

    // Products
    constexpr float Dot(const Vector2& other) const
    {
        return x * other.x + y * other.y;
    }

    constexpr float Cross(const Vector2& other) const
    {
        return x * other.y - y * other.x;
    }

    // Distance
    float Distance(const Vector2& other) const
    {
        return (*this - other).Length();
    }

    constexpr float DistanceSquared(const Vector2& other) const
    {
        return (*this - other).LengthSquared();
    }

    // Angle in radians
    float Angle() const
    {
        return std::atan2(y, x);
    }

    float AngleTo(const Vector2& other) const
    {
        return std::atan2(Cross(other), Dot(other));
    }

    // Rotate counterclockwise (radians)
    Vector2 Rotated(float angle) const
    {
        float c = std::cos(angle);
        float s = std::sin(angle);

        return {
            x * c - y * s,
            x * s + y * c
        };
    }

    void Rotate(float angle)
    {
        *this = Rotated(angle);
    }

    // Linear interpolation
    constexpr Vector2 Lerp(const Vector2& other, float t) const
    {
        return *this + (other - *this) * t;
    }

    // Perpendicular vector
    constexpr Vector2 Perpendicular() const
    {
        return {-y, x};
    }

    // Arithmetic operators
    constexpr Vector2 operator+(const Vector2& other) const
    {
        return {x + other.x, y + other.y};
    }

    constexpr Vector2 operator-(const Vector2& other) const
    {
        return {x - other.x, y - other.y};
    }

    constexpr Vector2 operator*(float scalar) const
    {
        return {x * scalar, y * scalar};
    }

    constexpr Vector2 operator/(float scalar) const
    {
        return {x / scalar, y / scalar};
    }

    constexpr Vector2 operator-() const
    {
        return {-x, -y};
    }

    constexpr Vector2 operator+() const
    {
        return *this;
    }

    // Component-wise operations
    constexpr Vector2 operator*(const Vector2& other) const
    {
        return {x * other.x, y * other.y};
    }

    constexpr Vector2 operator/(const Vector2& other) const
    {
        return {x / other.x, y / other.y};
    }

    // Assignment operators
    Vector2& operator+=(const Vector2& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector2& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vector2& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    Vector2& operator*=(const Vector2& other)
    {
        x *= other.x;
        y *= other.y;
        return *this;
    }

    Vector2& operator/=(const Vector2& other)
    {
        x /= other.x;
        y /= other.y;
        return *this;
    }

    // Comparison
    constexpr bool operator==(const Vector2& other) const
    {
        return x == other.x && y == other.y;
    }

    constexpr bool operator!=(const Vector2& other) const
    {
        return !(*this == other);
    }

    // Scalar * Vector
    friend constexpr Vector2 operator*(float scalar, const Vector2& v)
    {
        return v * scalar;
    }

    // Output stream
    friend std::ostream& operator<<(std::ostream& os, const Vector2& v)
    {
        return os << "Vector2(" << v.x << ", " << v.y << ")";
    }
};
