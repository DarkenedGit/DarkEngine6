#pragma once

#include "Math/Vector3f.h"
#include "Terrain/HeightMap.h"

namespace Dark::AI
{
    // coneDeg is the full field of view in degrees (half-angle is coneDeg * 0.5).
    struct SightQuery
    {
        Math::Vector3f            eye{};
        Math::Vector3f            forward{ Math::Vect3f::Z_AXIS };
        Math::Vector3f            target{};
        float                     coneDeg   = 70.0f;
        float                     range     = 25.0f;
        const Terrain::HeightMap* heightMap = nullptr;
    };

    bool sees(const SightQuery& query);
} // namespace Dark::AI
