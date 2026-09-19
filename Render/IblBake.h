#pragma once

#include "Assets/Image.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Render/IblSampling.h"

#include <cstdint>

namespace Dark
{

    struct IblBakeSettings
    {
        uint32_t equirectToCubeSize = 128;
        uint32_t irradianceSize     = 32;
        uint32_t prefilterSize      = 128;
        uint32_t prefilterMips      = 5;
        // Runtime defaults are cheaper than the RFC's 1024/128/1024. Tests pass explicit counts.
        uint32_t sampleCountIrr     = 64;
        uint32_t sampleCountPref    = 32;
        uint32_t brdfLutSize        = 128;
        uint32_t sampleCountLut     = 32;

        float maxRoughnessMip() const
        {
            return prefilterMips > 1u ? static_cast<float>(prefilterMips - 1u) : 0.0f;
        }
    };

    namespace IblBake
    {
        // False if sizes/samples are 0, prefilterMips*6 exceeds the 30-slot RTV heap, or mips exceed log2(prefilterSize)+1.
        bool validateSettings(const IblBakeSettings& settings);

        bool generateBrdfLut(float* rgOut, uint32_t size, uint32_t sampleCount);
        Math::Vector2f integrateBrdf(float ndotV, float roughness, uint32_t sampleCount);

        // Uniform white Li=1. Stores true E (not /π): E += Li; E *= π / N.
        Math::Vector3f convolveIrradianceUniformWhite(const Math::Vector3f& n, uint32_t sampleCount);

        Math::Vector3f sampleEquirect(const Image& image, const Math::Vector3f& dir);

        // Studio ambient equirect: zenith (0.12,0.16,0.22), horizon (0.35,0.38,0.42), no sun disc.
        bool fillStudioGradient(Image& out, uint32_t width = 128, uint32_t height = 64);
    }

} // namespace Dark
