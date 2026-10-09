#pragma once

#include "Terrain/GrassTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Dark::Terrain
{
    constexpr float kGrassPushMetres          = 0.55f;
    constexpr float kGrassShoveRadiusMetres   = 1.15f;
    constexpr float kGrassShoveInnerMetres    = 0.15f;
    constexpr float kGrassShoveSpeedMetres    = 0.50f;
    constexpr float kGrassShoveAwayWeight     = 0.75f;
    constexpr float kGrassShoveVelWeight      = 0.25f;
    constexpr float kGrassFootprintStepMetres = 0.35f;
    constexpr float kGrassFootprintLifeSec    = 0.50f;
    constexpr int   kGrassInteractorSlots     = 4;
    constexpr int   kGrassFootprintSlots      = 8;

    static_assert(kGrassInteractorSlots == 4, "grass interactor slots");
    static_assert(kGrassFootprintSlots == 8, "grass footprint slots");

    struct GrassShove
    {
        float x = 0.0f;
        float z = 0.0f;
    };

    struct GrassPlanarYaw
    {
        float x    = 0.0f;
        float z    = 0.0f;
        bool  bias = false;
    };

    inline float grassShoveFalloff(float dist, float radius)
    {
        if (!(radius > kGrassShoveInnerMetres))
            return dist <= kGrassShoveInnerMetres ? 1.0f : 0.0f;
        return Math::SmoothStep(radius, kGrassShoveInnerMetres, dist);
    }

    inline void grassShoveDirection(float bladeX, float bladeZ, float discX, float discZ, float velX, float velZ, bool bias, float& outX, float& outZ)
    {
        const float dx         = bladeX - discX;
        const float dz         = bladeZ - discZ;
        const float len2       = dx * dx + dz * dz;
        float       ax         = 0.0f;
        float       az         = 1.0f;
        const bool  degenerate = !(len2 > 1.0e-8f);
        if (!degenerate)
        {
            const float inv = 1.0f / std::sqrt(len2);
            ax              = dx * inv;
            az              = dz * inv;
        }

        const float speed2 = velX * velX + velZ * velZ;
        const bool  moving = bias && speed2 > kGrassShoveSpeedMetres * kGrassShoveSpeedMetres;
        if (!moving)
        {
            outX = ax;
            outZ = az;
            return;
        }

        const float invS = 1.0f / std::sqrt(speed2);
        const float vx   = velX * invS;
        const float vz   = velZ * invS;
        if (degenerate)
        {
            outX = vx;
            outZ = vz;
            return;
        }

        const float mx = ax * kGrassShoveAwayWeight + vx * kGrassShoveVelWeight;
        const float mz = az * kGrassShoveAwayWeight + vz * kGrassShoveVelWeight;
        const float m2 = mx * mx + mz * mz;
        if (m2 > 1.0e-8f)
        {
            const float invM = 1.0f / std::sqrt(m2);
            outX             = mx * invM;
            outZ             = mz * invM;
            return;
        }
        outX = vx;
        outZ = vz;
    }

    inline GrassShove grassShoveMetres(int lod, float bladeX, float bladeZ, const GrassInteractor* interactors, const GrassInteractor* footprints, float velX, float velZ, float pushMetres)
    {
        GrassShove shove{};
        if (lod < 0 || lod >= 2 || !interactors || !footprints || !(pushMetres > 0.0f))
            return shove;

        float best   = 0.0f;
        float winX   = 0.0f;
        float winZ   = 0.0f;
        bool  player = false;
        for (int i = 0; i < kGrassInteractorSlots; ++i)
        {
            const GrassInteractor& disc = interactors[i];
            if (!(disc.strength > 0.0f) || !(disc.radius > 0.0f))
                continue;
            const float dx    = bladeX - disc.x;
            const float dz    = bladeZ - disc.z;
            const float dist2 = dx * dx + dz * dz;
            if (dist2 > disc.radius * disc.radius)
                continue;
            const float fall = grassShoveFalloff(std::sqrt(dist2), disc.radius) * disc.strength;
            if (fall > best)
            {
                best   = fall;
                winX   = disc.x;
                winZ   = disc.z;
                player = i == 0;
            }
        }
        for (int i = 0; i < kGrassFootprintSlots; ++i)
        {
            const GrassInteractor& disc = footprints[i];
            if (!(disc.strength > 0.0f) || !(disc.radius > 0.0f))
                continue;
            const float dx    = bladeX - disc.x;
            const float dz    = bladeZ - disc.z;
            const float dist2 = dx * dx + dz * dz;
            if (dist2 > disc.radius * disc.radius)
                continue;
            const float fall = grassShoveFalloff(std::sqrt(dist2), disc.radius) * disc.strength;
            if (fall > best)
            {
                best   = fall;
                winX   = disc.x;
                winZ   = disc.z;
                player = false;
            }
        }
        if (!(best > 0.0f))
            return shove;

        float dirX = 0.0f;
        float dirZ = 1.0f;
        grassShoveDirection(bladeX, bladeZ, winX, winZ, velX, velZ, player, dirX, dirZ);
        shove.x = dirX * pushMetres * best;
        shove.z = dirZ * pushMetres * best;
        return shove;
    }

    inline uint16_t grassPackYawBits(float velX, float velZ)
    {
        const float speed2 = velX * velX + velZ * velZ;
        if (!(speed2 > kGrassShoveSpeedMetres * kGrassShoveSpeedMetres))
            return 0;
        const float yaw = std::atan2(velX, velZ);
        const float u   = (yaw + Math::Pi) / Math::TwoPi;
        int         q   = 1 + static_cast<int>(std::lround(u * 65534.0f));
        if (q < 1)
            q = 1;
        if (q > 65535)
            q = 65535;
        return static_cast<uint16_t>(q);
    }

    inline GrassPlanarYaw grassDecodeYawBits(uint16_t q)
    {
        GrassPlanarYaw decoded{};
        if (q == 0)
            return decoded;
        const float u   = (static_cast<float>(q) - 1.0f) / 65534.0f;
        const float yaw = u * Math::TwoPi - Math::Pi;
        decoded.bias    = true;
        decoded.x       = std::sin(yaw);
        decoded.z       = std::cos(yaw);
        return decoded;
    }

    inline void grassUnpackPlanarYaw(float packed, GrassPlanarYaw& current, GrassPlanarYaw& previous)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &packed, sizeof(bits));
        current  = grassDecodeYawBits(static_cast<uint16_t>(bits & 0xFFFFu));
        previous = grassDecodeYawBits(static_cast<uint16_t>(bits >> 16));
    }

    struct GrassTipIn
    {
        float height = 0.55f;
        float flex   = 0.65f;
        float windX  = 0.0f;
        float windZ  = 0.0f;
        float shoveX = 0.0f;
        float shoveZ = 0.0f;
    };

    struct GrassTip
    {
        float x = 0.0f;
        float z = 0.0f;
    };

    struct GrassLocal
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    inline GrassTip grassTipOffset(const GrassTipIn& in)
    {
        GrassTip tip{};
        tip.x              = (in.windX + in.shoveX) * in.flex;
        tip.z              = (in.windZ + in.shoveZ) * in.flex;
        const float maxLen = 0.85f * in.height;
        const float len2   = tip.x * tip.x + tip.z * tip.z;
        if (maxLen > 0.0f && len2 > maxLen * maxLen)
        {
            const float s = maxLen / std::sqrt(len2);
            tip.x *= s;
            tip.z *= s;
        }
        return tip;
    }

    inline GrassLocal grassBladeLocal(int lod, float t, const GrassTip& tip, float height)
    {
        const float tipLen2 = tip.x * tip.x + tip.z * tip.z;
        const float yFloor  = 0.20f * height;
        const float under   = height * height - tipLen2;
        const float yTip    = std::sqrt(std::max(under, yFloor * yFloor));

        const float u  = 1.0f - t;
        const float b0 = u * u;
        const float b1 = 2.0f * u * t;
        const float b2 = t * t;

        GrassLocal local{};
        if (lod <= 1)
        {
            const float p0x = 0.0f;
            const float p0y = 0.0f;
            const float p0z = 0.0f;
            const float p1x = tip.x * 0.25f;
            const float p1y = height * 0.55f;
            const float p1z = tip.z * 0.25f;
            local.x         = b0 * p0x + b1 * p1x + b2 * tip.x;
            local.y         = b0 * p0y + b1 * p1y + b2 * yTip;
            local.z         = b0 * p0z + b1 * p1z + b2 * tip.z;
            return local;
        }

        local.x = tip.x * t;
        local.y = yTip * t;
        local.z = tip.z * t;
        return local;
    }

} // namespace Dark::Terrain
