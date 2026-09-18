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
        uint32_t brdfLutSize        = 256;
        uint32_t sampleCountIrr     = 1024;
        uint32_t sampleCountPref    = 128;
        uint32_t sampleCountLut     = 1024;

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
    }

} // namespace Dark
