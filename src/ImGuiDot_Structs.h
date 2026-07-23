#pragma once

#include <cmath>
#include <gvc.h>
#include <imgui.h>

namespace ImGuiDot
{
    // A ImColor extracted or converted from the Graphviz format.
    struct Colour
    {
        ImColor colour;
        bool isValid;
    };

    // A 2D vector, it expand the ImVec2 adding some mathematical operators.
    struct Vec2: ImVec2
    {
        Vec2()
        {
            x = 0.0f;
            y = 0.0f;
        }

        Vec2(const float _x, const float _y)
        {
            x = _x;
            y = _y;
        }

        Vec2(const double _x, const double _y)
        {
            x = static_cast<float>(_x);
            y = static_cast<float>(_y);
        }

        Vec2(const ImVec2 &valore): ImVec2(valore) {}

        Vec2(const pointf &valore)
        {
            x = static_cast<float>(valore.x);
            y = static_cast<float>(valore.y);
        }

        Vec2 operator+(const Vec2 &valore) const
        {
            return { x + valore.x, y + valore.y };
        }

        Vec2 &operator+=(const Vec2 &valore)
        {
            x += valore.x;
            y += valore.y;

            return *this;
        }

        Vec2 operator-(const Vec2 &valore) const
        {
            return { x - valore.x, y - valore.y };
        }

        Vec2 &operator-=(const Vec2 &valore)
        {
            x -= valore.x;
            y -= valore.y;

            return *this;
        }

        Vec2 operator*(const float valore) const
        {
            return { x * valore, y * valore };
        }

        Vec2 &operator*=(const float valore)
        {
            x *= valore;
            y *= valore;

            return *this;
        }

        Vec2 operator/(const float valore) const
        {
            return { x / valore, y / valore };
        }

        Vec2 &operator/=(const float valore)
        {
            x /= valore;
            y /= valore;

            return *this;
        }

        [[nodiscard]]
        float Length() const
        {
            return std::sqrtf(x * x + y * y);
        }

        bool Normalize()
        {
            const float lunghezza = Length();
            if (lunghezza < 1e-4f) return false;
            *this /= lunghezza;

            return true;
        }
    };

    inline Vec2 operator+(const pointf &a, const Vec2 &b)
    {
        return { a.x + b.x, a.y + b.y };
    }

    inline Vec2 operator-(const pointf &a, const Vec2 &b)
    {
        return { a.x - b.x, a.y - b.y };
    }

    inline Vec2 operator*(const pointf &a, const Vec2 &b)
    {
        return { a.x * b.x, a.y * b.y };
    }

    inline Vec2 operator/(const pointf &a, const Vec2 &b)
    {
        return { a.x / b.x, a.y / b.y };
    }
}
