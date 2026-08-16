#pragma once

#include "Math/Vector3f.h"

#include <cstdint>
#include <string>

namespace Dark
{

    struct FloatRange
    {
        float min = 0.0f;
        float max = 1.0f;

        float sample(float u01) const // u01 in [0,1]
        {
            return min + (max - min) * u01;
        }
    };

    // Authorable emitter definition (JSON-friendly, edited in the Editor UI).
    struct ParticleEmitterDesc
    {
        std::string name = "Emitter";

        uint32_t maxParticles = 512;
        float    emissionRate = 40.0f; // particles / second
        float    duration     = 0.0f;  // seconds; 0 = infinite while playing
        bool     looping      = true;
        bool     prewarm      = false;

        FloatRange lifetime{ 0.8f, 1.6f };
        FloatRange startSpeed{ 1.0f, 3.0f };
        FloatRange startSize{ 0.15f, 0.35f };
        FloatRange endSize{ 0.02f, 0.10f };

        float startColor[4]{ 1.0f, 0.7f, 0.25f, 1.0f };
        float endColor[4]{ 1.0f, 0.15f, 0.05f, 0.0f };

        Math::Vector3f gravity{ 0.0f, -2.0f, 0.0f };
        Math::Vector3f direction{ 0.0f, 1.0f, 0.0f }; // emission cone axis
        float          spreadDegrees = 25.0f;

        enum class Shape : uint8_t
        {
            Point = 0,
            Box,
            Sphere,
        };
        Shape          shape = Shape::Point;
        Math::Vector3f shapeSize{ 0.25f, 0.0f, 0.25f }; // box half-extents / sphere radius (x)

        bool  additiveBlend   = true;
        float simulationSpeed = 1.0f;

        enum class RenderMode : uint8_t
        {
            Billboard = 0,
            Ribbon,
        };
        RenderMode renderMode = RenderMode::Billboard;

        // Independent camera-facing strips. New particles round-robin across ribbons.
        uint32_t ribbonCount   = 1;
        float    ribbonUvScale = 1.0f; // U tiling along the strip
    };

    constexpr uint32_t kMaxRibbonCount = 16;

    // Live particle instance (CPU sim).
    struct Particle
    {
        Math::Vector3f position{};
        Math::Vector3f velocity{};
        float          life    = 0.0f;
        float          maxLife = 1.0f;
        float          size0   = 0.2f;
        float          size1   = 0.05f;
        float          color0[4]{ 1, 1, 1, 1 };
        float          color1[4]{ 1, 1, 1, 0 };
        float          rotation  = 0.0f;
        uint32_t       ribbonId  = 0;
        uint32_t       seq       = 0;
        bool           alive     = false;
    };

    // One control point on a ribbon (age already baked into size/color).
    struct RibbonNode
    {
        Math::Vector3f position{};
        float          size = 0.2f;
        float          color[4]{ 1, 1, 1, 1 };
    };

    // GPU billboard vertex (CPU expands quads).
    struct ParticleVertex
    {
        float px, py, pz;
        float u, v;
        float r, g, b, a;
    };
    static_assert(sizeof(ParticleVertex) == 36, "particle vertex layout");

} // namespace Dark
