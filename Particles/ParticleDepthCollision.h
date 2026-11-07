#pragma once

#include "Math/MathDefines.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Particles/ParticleTypes.h"

#include <cmath>
#include <cstdint>

namespace Dark
{

    struct ParticleProjectUvResult
    {
        bool  inside = false;
        float ndcX   = 0.0f;
        float ndcY   = 0.0f;
        float u      = 0.0f;
        float v      = 0.0f;
        float clipW  = 0.0f;
    };

    struct ParticleSurfaceViewZResult
    {
        bool  valid = false;
        float viewZ = 0.0f;
    };

    struct ParticleDepthHitResult
    {
        bool  hit   = false;
        float delta = 0.0f;
        float shell = 0.0f;
    };

    struct ParticleReflectResult
    {
        Math::Vector3f velocity{};
    };

    struct ParticleBirthSampleResult
    {
        Math::Vector3f position{};
        Math::Vector3f direction{};
        Math::Vector3f velocity{};
        float          life     = 0.0f;
        float          maxLife  = 0.0f;
        float          size0    = 0.0f;
        float          size1    = 0.0f;
        float          rotation = 0.0f;
        uint32_t       color0   = 0; // RGBA8
        uint32_t       color1   = 0; // RGBA8
        uint32_t       seed     = 0;
    };

    inline ParticleProjectUvResult particleProjectUv(const Math::Vector3f& world, const Math::Matrix4f& viewProj)
    {
        ParticleProjectUvResult result;
        const Math::Vector4f    clip = viewProj * Math::Vector4f(world, 1.0f);
        result.clipW                 = clip.w;
        if (!(clip.w > 0.0f))
            return result;

        result.ndcX   = clip.x / clip.w;
        result.ndcY   = clip.y / clip.w;
        result.u      = result.ndcX * 0.5f + 0.5f;
        result.v      = -result.ndcY * 0.5f + 0.5f; // VelocityUv
        result.inside = result.u >= 0.0f && result.u < 1.0f && result.v >= 0.0f && result.v < 1.0f;
        return result;
    }

    inline ParticleSurfaceViewZResult particleSurfaceViewZ(float depth, float nearZ)
    {
        ParticleSurfaceViewZResult result;
        if (depth <= 0.0f)
            return result;

        const float d = depth > 1.0e-7f ? depth : 1.0e-7f;
        result.valid  = true;
        result.viewZ  = nearZ / d;
        return result;
    }

    // particleDepthHit: delta in [-bias, shell]; viewZ <= near misses.
    inline ParticleDepthHitResult particleDepthHit(float particleViewZ, float surfaceViewZ, float nearZ, float bias, float thickness, float speed, float substepDt)
    {
        ParticleDepthHitResult result;
        float                  pad = speed * substepDt;
        if (pad < thickness)
            pad = thickness;
        if (pad > 0.75f)
            pad = 0.75f;
        result.shell = pad;
        result.delta = particleViewZ - surfaceViewZ;
        if (!(particleViewZ > nearZ))
            return result;
        result.hit = result.delta >= -bias && result.delta <= result.shell;
        return result;
    }

    inline float particleResolveViewZ(float surfaceViewZ, float bias)
    {
        return surfaceViewZ - bias;
    }

    // particleReflectVelocity: normal opposes incoming velocity.
    inline ParticleReflectResult particleReflectVelocity(Math::Vector3f velocity, Math::Vector3f normal, float restitution, float friction)
    {
        ParticleReflectResult result;
        const float           lenSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        if (!(lenSq > 1.0e-10f))
        {
            result.velocity = velocity;
            return result;
        }

        const float    invLen = 1.0f / std::sqrt(lenSq);
        Math::Vector3f n(normal.x * invLen, normal.y * invLen, normal.z * invLen);
        float          vn = velocity.Dot(n);
        if (vn > 0.0f)
        {
            n  = -n;
            vn = -vn;
        }

        auto saturate = [](float x) -> float {
            if (x < 0.0f)
                return 0.0f;
            if (x > 1.0f)
                return 1.0f;
            return x;
        };

        const Math::Vector3f tangent = velocity - n * vn;
        result.velocity              = tangent * (1.0f - saturate(friction)) - n * vn * saturate(restitution);
        return result;
    }

    // Same argument order and one ClampDepthForReconstruct as ReconstructWorldPos.
    inline Math::Vector3f particleReconstructWorld(float ndcX, float ndcY, float depth, const Math::Matrix4f& invViewProj)
    {
        const float          clamped = depth > 1.0e-7f ? depth : 1.0e-7f;
        const Math::Vector4f w       = invViewProj * Math::Vector4f(ndcX, ndcY, clamped, 1.0f);
        const float          denom   = w.w > 1.0e-6f ? w.w : 1.0e-6f;
        const float          invW    = 1.0f / denom;
        return Math::Vector3f(w.x * invW, w.y * invW, w.z * invW);
    }

    inline Math::Vector3f particleDepthNormal(const Math::Vector3f& center, const Math::Vector3f& alongX, const Math::Vector3f& alongY, const Math::Vector3f& cameraPos)
    {
        Math::Vector3f n = (alongX - center).Cross(alongY - center);
        const float    lenSq = n.x * n.x + n.y * n.y + n.z * n.z;
        if (!(lenSq > 1.0e-10f))
            return Math::Vector3f(0.0f, 0.0f, 0.0f);

        const float invLen = 1.0f / std::sqrt(lenSq);
        n.x *= invLen;
        n.y *= invLen;
        n.z *= invLen;
        if (n.Dot(cameraPos - center) < 0.0f)
            n = -n;
        return n;
    }

    inline ParticleBirthSampleResult particleBirthSample(const ParticleEmitterDesc& desc, uint32_t hash)
    {
        // xorshift32(0) stays 0, so a zero hash starts at 1.
        uint32_t state = hash == 0u ? 1u : hash;
        auto     next01 = [&state]() -> float {
            uint32_t x = state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            state = x;
            return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
        };

        auto normalizeOr = [](const Math::Vector3f& v, const Math::Vector3f& fallback) -> Math::Vector3f {
            const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
            if (!(lenSq > 1.0e-10f))
                return fallback;
            const float inv = 1.0f / std::sqrt(lenSq);
            return Math::Vector3f(v.x * inv, v.y * inv, v.z * inv);
        };

        auto packRgba8 = [](const float c[4]) -> uint32_t {
            auto u8 = [](float v) -> uint32_t {
                if (v < 0.0f)
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                return static_cast<uint32_t>(v * 255.0f + 0.5f);
            };
            return (u8(c[0]) << 24) | (u8(c[1]) << 16) | (u8(c[2]) << 8) | u8(c[3]);
        };

        ParticleBirthSampleResult sample;
        sample.seed    = hash;
        sample.maxLife = desc.lifetime.sample(next01());
        if (sample.maxLife < 0.05f)
            sample.maxLife = 0.05f;
        sample.life = sample.maxLife;

        switch (desc.shape)
        {
        case ParticleEmitterDesc::Shape::Box:
            sample.position = Math::Vector3f((next01() * 2.0f - 1.0f) * desc.shapeSize.x, (next01() * 2.0f - 1.0f) * desc.shapeSize.y, (next01() * 2.0f - 1.0f) * desc.shapeSize.z);
            break;
        case ParticleEmitterDesc::Shape::Sphere:
        {
            const float radius = desc.shapeSize.x * std::cbrt(next01());
            const float theta  = next01() * Math::TwoPi;
            const float z      = next01() * 2.0f - 1.0f;
            const float ring   = std::sqrt(z * z < 1.0f ? 1.0f - z * z : 0.0f);
            sample.position    = Math::Vector3f(radius * ring * std::cos(theta), radius * ring * std::sin(theta), radius * z);
            break;
        }
        case ParticleEmitterDesc::Shape::Point:
        default:
            sample.position = Math::Vector3f(0.0f, 0.0f, 0.0f);
            break;
        }

        Math::Vector3f axis = normalizeOr(desc.direction, Math::Vector3f::Y_AXIS);
        const float    spreadRad = desc.spreadDegrees * Math::DegToRad;
        if (spreadRad <= 1.0e-5f)
        {
            sample.direction = axis;
        }
        else
        {
            const float cosMax   = std::cos(spreadRad);
            const float cosTheta = cosMax + (1.0f - cosMax) * next01();
            const float sinTheta = std::sqrt(cosTheta * cosTheta < 1.0f ? 1.0f - cosTheta * cosTheta : 0.0f);
            const float phi      = next01() * Math::TwoPi;

            const Math::Vector3f helper  = (std::fabs(axis.y) < 0.99f) ? Math::Vector3f::Y_AXIS : Math::Vector3f::X_AXIS;
            const Math::Vector3f tangent = normalizeOr(axis.Cross(helper), Math::Vector3f::X_AXIS);
            const Math::Vector3f bitangent = axis.Cross(tangent);
            const float          cp       = std::cos(phi);
            const float          sp       = std::sin(phi);
            sample.direction              = normalizeOr(axis * cosTheta + tangent * (sinTheta * cp) + bitangent * (sinTheta * sp), axis);
        }

        const float speed = desc.startSpeed.sample(next01());
        sample.velocity   = sample.direction * speed;
        sample.size0      = desc.startSize.sample(next01());
        sample.size1      = desc.endSize.sample(next01());
        sample.color0     = packRgba8(desc.startColor);
        sample.color1     = packRgba8(desc.endColor);
        sample.rotation   = next01() * Math::TwoPi;
        return sample;
    }

} // namespace Dark
