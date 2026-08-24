#pragma once

#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{

class Camera3D;

constexpr int kMaxShadowCascades = 3;

struct ShadowSettings
{
    uint32_t mapSize       = 2048;
    int      cascadeCount  = kMaxShadowCascades;
    float    maxDistance   = 280.0f;
    float    splitLambda   = 0.65f;
    float    casterMargin  = 24.0f;  // extra light-camera pullback (metres)
    float    depthBias     = 0.05f;  // receiver bias in world metres
};

struct CascadeData
{
    Math::Matrix4f viewProj;
    float          splitNear = 0.0f;
    float          splitFar  = 0.0f;
    float          zRange    = 1.0f; // light-space ortho far-near, metres
};

// GPU cbuffer (b1). Keep in sync with content/shaders/Shadow.hlsli.
struct ShadowConstants
{
    float cascadeViewProj[kMaxShadowCascades][16];
    float cascadeSplits[4]; // x,y,z = cascade far in view-Z, w = map size
    float params[4];        // world-space bias, strength, cascadeCount, array-slice offset
    float cameraLook[3];
    float pad;
    float cascadeInvZ[4]; // xyz = 1 / light-space Z range (world metres -> NDC)
};

static_assert(sizeof(ShadowConstants) == (48 + 4 + 4 + 4 + 4) * sizeof(float), "shadow cbuffer size");

void computePracticalSplits(
    float nearZ,
    float farZ,
    int cascadeCount,
    float lambda,
    float outSplits[kMaxShadowCascades]);

void extractFrustumCorners(
    const Camera3D& camera,
    float nearZ,
    float farZ,
    Math::Vector3f outCorners[8]);

// Texel-snapped light ortho around the slice's bounding sphere. Scene AABB
// only pulls the near plane back so casters in front of the slice still write.
bool buildCascadeMatrix(
    const Math::Vector3f corners[8],
    const Math::Vector3f& lightDirToward,
    const Math::Aabb3f& sceneBounds,
    float casterMargin,
    uint32_t mapSize,
    CascadeData& out);

void packShadowConstants(
    ShadowConstants& out,
    const CascadeData cascades[],
    int cascadeCount,
    uint32_t mapSize,
    float depthBias,
    float strength,
    const Math::Vector3f& cameraLook,
    float sliceOffset = 0.0f);

} // namespace Dark
