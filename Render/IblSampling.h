#pragma once

#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

#include <cmath>
#include <cstdint>

namespace Dark
{

    inline Math::Vector3f iblRotateY(const Math::Vector3f& d, float rad)
    {
        const float s = std::sinf(rad);
        const float c = std::cosf(rad);
        return Math::Vector3f(d.x * c + d.z * s, d.y, -d.x * s + d.z * c);
    }

    inline Math::Vector3f cubeFaceDir(const Math::Vector2f& clipXY, uint32_t face)
    {
        Math::Vector3f dir;
        if (face == 0)
            dir = Math::Vector3f(1.0f, clipXY.y, -clipXY.x);
        else if (face == 1)
            dir = Math::Vector3f(-1.0f, clipXY.y, clipXY.x);
        else if (face == 2)
            dir = Math::Vector3f(clipXY.x, 1.0f, -clipXY.y);
        else if (face == 3)
            dir = Math::Vector3f(clipXY.x, -1.0f, clipXY.y);
        else if (face == 4)
            dir = Math::Vector3f(clipXY.x, clipXY.y, 1.0f);
        else
            dir = Math::Vector3f(-clipXY.x, clipXY.y, -1.0f);
        dir.Normalize();
        return dir;
    }

    inline Math::Vector2f dirToEquirectUv(Math::Vector3f dir)
    {
        dir.Normalize();
        const float y   = Math::Clamp(dir.y, -1.0f, 1.0f);
        const float phi = std::atan2(dir.x, dir.z);
        const float theta = std::acos(y);
        return Math::Vector2f(phi * (0.5f * Math::InvPi) + 0.5f, theta * Math::InvPi);
    }

    inline float radicalInverseVdC(uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<float>(bits) * 2.3283064365386963e-10f;
    }

    inline Math::Vector2f hammersley(uint32_t i, uint32_t n)
    {
        const float invN = n > 0 ? 1.0f / static_cast<float>(n) : 0.0f;
        return Math::Vector2f(static_cast<float>(i) * invN, radicalInverseVdC(i));
    }

    inline void iblTangentBasis(const Math::Vector3f& n, Math::Vector3f& tangent, Math::Vector3f& bitangent)
    {
        const Math::Vector3f up = (std::fabs(n.z) < 0.999f) ? Math::Vector3f(0.0f, 0.0f, 1.0f) : Math::Vector3f(1.0f, 0.0f, 0.0f);
        tangent                 = up.Cross(n);
        tangent.Normalize();
        bitangent = n.Cross(tangent);
    }

    inline Math::Vector3f iblTangentToWorld(const Math::Vector3f& n, const Math::Vector3f& tangentLocal)
    {
        Math::Vector3f tangent, bitangent;
        iblTangentBasis(n, tangent, bitangent);
        Math::Vector3f world = tangent * tangentLocal.x + bitangent * tangentLocal.y + n * tangentLocal.z;
        world.Normalize();
        return world;
    }

    inline Math::Vector3f cosineSampleHemisphere(const Math::Vector2f& xi, const Math::Vector3f& n)
    {
        const float phi      = Math::TwoPi * xi.x;
        const float cosTheta = std::sqrt(std::fmax(0.0f, 1.0f - xi.y));
        const float sinTheta = std::sqrt(std::fmax(0.0f, xi.y));
        const Math::Vector3f t(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
        return iblTangentToWorld(n, t);
    }

    inline Math::Vector3f importanceSampleGgx(const Math::Vector2f& xi, const Math::Vector3f& n, float roughness)
    {
        const float a        = roughness * roughness;
        const float phi      = Math::TwoPi * xi.x;
        const float denom    = std::fmax(1.0f + (a * a - 1.0f) * xi.y, 1.0e-6f);
        const float cosTheta = std::sqrt(std::fmax(0.0f, (1.0f - xi.y) / denom));
        const float sinTheta = std::sqrt(std::fmax(0.0f, 1.0f - cosTheta * cosTheta));
        const Math::Vector3f h(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
        return iblTangentToWorld(n, h);
    }

} // namespace Dark
