#pragma once

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{

// One shadowed light is one spot slice or six point faces. The foliage view cap
// includes this so a higher light cap cannot silently drop casters.
constexpr uint32_t kMaxShadowedLocalLights    = 4;
constexpr uint32_t kLocalShadowSlicesPerFrame = 12;
constexpr uint32_t kLocalShadowFrames         = 2;
constexpr uint32_t kLocalShadowRecordCount    = kMaxShadowedLocalLights * kLocalShadowFrames;
constexpr int      kLocalShadowPointFaces     = 6;

struct LocalShadowSettings
{
    uint32_t mapSize           = 1024;
    uint32_t slicesPerFrame    = kLocalShadowSlicesPerFrame;
    uint32_t maxShadowedLights = kMaxShadowedLocalLights;
    float    depthBias         = 0.05f; // metres, same default as ShadowSettings
    float    nearPlane         = 0.05f;
};

// HLSL (content/shaders/LocalShadow.hlsli), pack_matrix(row_major), field for field:
// struct GpuLocalShadowFace { float4x4 viewProj; float zn; float zf; float tanHalfFov; float slice; };
// struct GpuLocalShadowRecord {
//     float faceCount; float mapSize; float depthBias; float strength;
//     GpuLocalShadowFace faces[6];
// };
struct GpuLocalShadowFace
{
    float viewProj[16]; // row-major, mul(world, viewProj), no transpose
    float zn;
    float zf;
    float tanHalfFov;
    float slice; // includes the frame base
};
static_assert(sizeof(GpuLocalShadowFace) == 20 * sizeof(float), "local shadow face");

struct GpuLocalShadowRecord
{
    float              faceCount; // 1 spot, 6 point
    float              mapSize;
    float              depthBias; // metres
    float              strength;  // 1 when allocated; the shader lerps toward the sample
    GpuLocalShadowFace faces[kLocalShadowPointFaces];
};
static_assert(sizeof(GpuLocalShadowRecord) == 124 * sizeof(float), "local shadow record");

// Spot frames the outer cone. Point faces are 90 degree cube faces in D3D order.
// Both return view * proj. The caller rejects a non-finite result or zf <= zn.
Math::Matrix4f buildSpotShadowViewProj(const Math::Vector3f& pos, const Math::Vector3f& dirTowardBase, float range, float outerConeDeg, float zn);
Math::Matrix4f buildPointFaceViewProj(const Math::Vector3f& pos, int face, float range, float zn);

// 0..5, same branches as SelectPointShadowFace. fromLight is worldPos - lightPos.
int selectPointShadowFace(const Math::Vector3f& fromLight);

// Receiver bias in NDC, including the 4/mapSize clamp.
float localShadowNdcBias(float depthBiasM, float zn, float zf, float viewZ, float mapSize);

} // namespace Dark
