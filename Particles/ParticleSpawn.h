#pragma once

#include "Particles/ParticleTypes.h"

#include <cmath>
#include <cstdint>

namespace Dark
{

    struct ParticleSpawnStep
    {
        uint32_t head       = 0;
        uint32_t spawnCount = 0; // 0 .. capacity
        float    carry      = 0.0f; // fractional, in [0, 1)
    };

    struct ParticlePoolDecision
    {
        bool forceCpu      = false;
        bool recordGpuPool = false;
    };

    // capacity == 0 returns before modulo. Unsigned modulo by zero is undefined.
    inline ParticleSpawnStep particleAdvanceSpawn(float carry, float rate, float dtUsed, uint32_t capacity, uint32_t head, uint32_t burst)
    {
        if (capacity == 0 || !std::isfinite(carry) || !std::isfinite(rate) || !std::isfinite(dtUsed))
            return ParticleSpawnStep{ head, 0u, 0.0f };

        const double accumulated = static_cast<double>(carry) + static_cast<double>(rate) * static_cast<double>(dtUsed);
        if (!std::isfinite(accumulated))
            return ParticleSpawnStep{ head, 0u, 0.0f };

        const double wholeD = std::floor(accumulated);
        double       fracD  = accumulated - wholeD;
        if (!(fracD >= 0.0) || !(fracD < 1.0))
            fracD = 0.0;

        uint32_t whole = 0;
        if (wholeD > 0.0)
        {
            if (wholeD >= static_cast<double>(capacity))
                whole = capacity;
            else
                whole = static_cast<uint32_t>(wholeD);
        }

        // uint32 whole + burst wraps; a wrapped 0 would spawn nothing instead of a full ring.
        const uint64_t sum   = static_cast<uint64_t>(whole) + static_cast<uint64_t>(burst);
        const uint32_t spawn = sum >= static_cast<uint64_t>(capacity) ? capacity : static_cast<uint32_t>(sum);
        const uint32_t newHead =
            static_cast<uint32_t>((static_cast<uint64_t>(head) + static_cast<uint64_t>(spawn)) % static_cast<uint64_t>(capacity));
        return ParticleSpawnStep{ newHead, spawn, static_cast<float>(fracD) };
    }

    inline bool particleSlotInSpawnWindow(uint32_t slot, uint32_t head, uint32_t spawnCount, uint32_t capacity)
    {
        if (spawnCount == 0 || capacity == 0 || slot >= capacity)
            return false;
        // dist is in 0 .. capacity-1, so spawnCount == capacity would miss the slot at dist 0.
        if (spawnCount >= capacity)
            return true;
        const uint32_t dist = static_cast<uint32_t>(
            (static_cast<uint64_t>(head) + static_cast<uint64_t>(capacity) - static_cast<uint64_t>(slot)) % static_cast<uint64_t>(capacity));
        return dist >= 1u && dist <= spawnCount;
    }

    inline ParticlePoolDecision particlePoolDecision(ParticleSimMode mode, uint32_t maxParticles, bool gpuParticlesEnabled, bool poolAccepted)
    {
        ParticlePoolDecision decision{};
        if (mode != ParticleSimMode::Gpu)
            return decision;
        if (gpuParticlesEnabled && poolAccepted)
        {
            decision.recordGpuPool = true;
            return decision;
        }
        if (maxParticles <= kMaxCpuParticles)
            decision.forceCpu = true;
        return decision;
    }

} // namespace Dark
