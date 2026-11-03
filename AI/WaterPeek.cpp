#include "AI/WaterPeek.h"

#include <cmath>

namespace Dark::AI
{

    namespace
    {
        constexpr float kWetMargin    = 0.25f;
        constexpr float kPeerRange    = 9.0f;
        constexpr float kPeerMaxDepth = 2.6f;
        constexpr float kPeerHalfDeg  = 55.0f;
    }

    bool findWaterBeside(GroundHeightFn heightAt, void* user, const Math::Vector3f& at, float waterLevel, Math::Vector3f& outDir)
    {
        outDir = {};
        if (!heightAt || waterLevel < -1.0e8f)
            return false;
        constexpr float kRadii[] = { 2.0f, 3.5f, 5.0f };
        constexpr int   kSpokes  = 12;
        float           sumX = 0.0f;
        float           sumZ = 0.0f;
        int             wet  = 0;
        for (int s = 0; s < kSpokes; ++s)
        {
            const float a  = 6.2831853f * static_cast<float>(s) / static_cast<float>(kSpokes);
            const float dx = std::sin(a);
            const float dz = std::cos(a);
            for (float r : kRadii)
            {
                if (heightAt(user, at.x + dx * r, at.z + dz * r) < waterLevel - kWetMargin)
                {
                    sumX += dx;
                    sumZ += dz;
                    ++wet;
                    break;
                }
            }
        }
        const float len = std::sqrt(sumX * sumX + sumZ * sumZ);
        if (wet == 0 || wet == kSpokes || len < 1.0e-3f)
            return false;
        outDir = Math::Vector3f{ sumX / len, 0.0f, sumZ / len };
        return true;
    }

    bool peerSpotsSwimmer(const Math::Vector3f& guardPos, const Math::Vector3f& forward, const Math::Vector3f& playerPos, float playerDepth)
    {
        if (playerDepth > kPeerMaxDepth)
            return false;
        const float dx   = playerPos.x - guardPos.x;
        const float dz   = playerPos.z - guardPos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > kPeerRange)
            return false;
        if (dist < 1.0e-3f)
            return true;
        const float flen = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        if (flen < 1.0e-4f)
            return false;
        const float cosA = (forward.x * dx + forward.z * dz) / (flen * dist);
        return cosA >= std::cos(kPeerHalfDeg * 0.0174533f);
    }

} // namespace Dark::AI
