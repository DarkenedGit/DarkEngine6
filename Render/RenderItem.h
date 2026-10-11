#pragma once

#include "Assets/AssetHandle.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"

#include <cstdint>

namespace Dark
{

class Mesh;
class MeshPipeline;
struct AnimPose;

constexpr uint32_t kMaxStaticItems = 4096;
constexpr uint32_t kMaxBatches     = 1024;
constexpr uint32_t kMaxStaticViews = 16;

enum class RenderItemKind : uint8_t
{
    Static = 0,
    Skinned,
    Translucent,
};

// Splits batches the host records under the existing profiler scopes.
enum class StaticPopulation : uint8_t
{
    OpaqueMeshes = 0,
    Projectiles  = 1,
    OpaqueModels = 2,
};

struct RenderItem
{
    const Mesh* mesh             = nullptr;
    AssetID     material         = NULL_ASSET;
    Math::Matrix4f world         = Math::Matrix4f::IDENTITY;
    Math::Matrix4f prevWorld     = Math::Matrix4f::IDENTITY;
    Math::AABox3f  worldAabb{}; // World space. Empty() is not culled.
    float          color[4]{};
    RenderItemKind kind          = RenderItemKind::Static;
    StaticPopulation population  = StaticPopulation::OpaqueModels;
    bool           castShadow    = true;
    bool           alphaMask     = false;
    const AnimPose* pose         = nullptr;
    const MeshPipeline* gbufferPso = nullptr;
};

// Field order is the sort order. Do not memcmp; padding sits before material.
struct BatchKey
{
    uint32_t population = 0;
    uint32_t meshKey    = 0;
    uint32_t alphaMask  = 0;
    AssetID  material   = NULL_ASSET;
};

struct MeshInstanceGpu
{
    float world[16];
    float prevWorld[16];
    float color[4];
};
static_assert(sizeof(MeshInstanceGpu) == 144, "static instance");

struct MeshBatchConstants
{
    float    viewProj[16];
    float    prevViewProj[16];
    float    roughness;
    float    metallic;
    float    ao;
    float    normalScale;
    float    alphaCutoff;
    float    alphaModeMask;
    uint32_t baseIndex;
    uint32_t pad;
};
static_assert(sizeof(MeshBatchConstants) == 40 * sizeof(uint32_t), "batch CB");

struct MeshDepthBatchConstants
{
    float    lightViewProj[16];
    uint32_t baseIndex;
    uint32_t pad[3];
};
static_assert(sizeof(MeshDepthBatchConstants) == 20 * sizeof(uint32_t), "depth batch CB");

// Forward opaque batches. Lighting floats are copied per batch. Shadow CBV stays once per view.
struct MeshForwardBatchConstants
{
    float    viewProj[16];
    float    lightDirWS[3];
    float    ambientScale;
    float    lightColor[3];
    float    padLight;
    float    cameraPos[3];
    float    lighting;
    float    normalScale;
    float    ao;
    float    alphaCutoff;
    float    alphaModeMask;
    uint32_t baseIndex;
    uint32_t pad[3];
};
static_assert(sizeof(MeshForwardBatchConstants) == 36 * sizeof(uint32_t), "forward batch");

// Layout matches D3D12_DRAW_INDEXED_ARGUMENTS. StartInstanceLocation stays 0; the batch base is a root constant.
struct DrawIndexedArgs
{
    uint32_t indexCountPerInstance = 0;
    uint32_t instanceCount         = 0;
    uint32_t startIndexLocation    = 0;
    int32_t  baseVertexLocation    = 0;
    uint32_t startInstanceLocation = 0;
};

constexpr uint32_t kCullAlwaysVisible = 1u;
constexpr uint32_t kCullCastShadow    = 2u;

// Scalar fields. A float3 here would pad differently from this layout in FXC.
struct MeshCullAabb
{
    float    center[3]{};
    uint32_t flags = 0;
    float    extent[3]{};
    uint32_t gbufferBatch = 0;
    uint32_t depthBatch   = 0;
    uint32_t pad[3]{};
};
static_assert(sizeof(MeshCullAabb) == 48, "cull aabb");

struct GpuBatchRecord
{
    uint32_t indexCount = 0;
    uint32_t itemBegin  = 0;
    uint32_t itemCount  = 0;
    uint32_t _pad       = 0;
};
static_assert(sizeof(GpuBatchRecord) == 16, "batch record");

inline bool batchKeyLess(const BatchKey& a, const BatchKey& b)
{
    if (a.population != b.population)
        return a.population < b.population;
    if (a.meshKey != b.meshKey)
        return a.meshKey < b.meshKey;
    if (a.alphaMask != b.alphaMask)
        return a.alphaMask < b.alphaMask;
    return a.material < b.material;
}

inline bool batchKeyEqual(const BatchKey& a, const BatchKey& b)
{
    return !batchKeyLess(a, b) && !batchKeyLess(b, a);
}

BatchKey batchKeyOf(const RenderItem& item);

struct GBufferBatch
{
    BatchKey    key{};
    uint32_t    itemBegin = 0;
    uint32_t    itemCount = 0;
    const Mesh* mesh      = nullptr;
};

struct DepthBatch
{
    uint32_t    population = 0;
    uint32_t    meshKey    = 0;
    uint32_t    itemBegin  = 0;
    uint32_t    itemCount  = 0;
    const Mesh* mesh       = nullptr;
};

// Farther view-space Z draws first. Equal Z keeps the incoming order.
struct TranslucentDraw
{
    Math::AABox3f worldAabb{};
    uint32_t      user = 0;
};

float translucentViewZ(const Math::AABox3f& worldAabb, const Math::Matrix4f& view);
void  sortTranslucent(TranslucentDraw* draws, uint32_t count, const Math::Matrix4f& view);

} // namespace Dark
