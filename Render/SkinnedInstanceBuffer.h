#pragma once

#include "Render/RenderItem.h"
#include "Render/SkinningUploadRing.h"

#include <cstdint>
#include <memory>
#include <vector>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace Dark
{

class GpuResourceCache;
class Mesh;

// One skinned part. world and prevWorld are already part.localToRoot times the entity matrix.
// A failed submit appends nothing. Hosts submit opaque parts or roll the character back.
struct SkinnedPartSubmit
{
    const Mesh*      mesh       = nullptr;
    AssetID          material   = NULL_ASSET;
    Math::Matrix4f   world      = Math::Matrix4f::IDENTITY;
    Math::Matrix4f   prevWorld  = Math::Matrix4f::IDENTITY;
    float            color[4]{};
    StaticPopulation population = StaticPopulation::OpaqueModels;
    bool             alphaMask  = false;
    bool             castShadow = true;
    const AnimPose*  pose       = nullptr;
};

// Palette upload and skinned instance upload. Two frame slots.
// One pose is 8192 bytes. The shader reads it as four 2048-byte structured elements (SM 5.0 limit).
// G-buffer root is 47 DWORDs. Depth root is 26. Forward root is 45. The immediate skinned signature is not extended.
// A failed forward PSO leaves g-buffer and depth instancing on. No compute and no ExecuteIndirect.
// DrawIndexedInstanced keeps StartInstanceLocation at 0.
class SkinnedInstanceBuffer
{
public:
    static constexpr uint32_t kFrameSlots = 2;

    static constexpr UINT kGbConstants = 0;
    static constexpr UINT kGbMaterial  = 1;
    static constexpr UINT kGbInstances = 2;
    static constexpr UINT kGbIndices   = 3;
    static constexpr UINT kGbPalettes  = 4;

    static constexpr UINT kDepthConstants = 0;
    static constexpr UINT kDepthInstances = 1;
    static constexpr UINT kDepthIndices   = 2;
    static constexpr UINT kDepthPalettes  = 3;

    static constexpr UINT kForwardConstants = 0;
    static constexpr UINT kForwardMaterial  = 1;
    static constexpr UINT kForwardShadow    = 2;
    static constexpr UINT kForwardInstances = 3;
    static constexpr UINT kForwardIndices   = 4;
    static constexpr UINT kForwardPalettes  = 5;

    SkinnedInstanceBuffer();
    ~SkinnedInstanceBuffer();
    SkinnedInstanceBuffer(const SkinnedInstanceBuffer&)            = delete;
    SkinnedInstanceBuffer& operator=(const SkinnedInstanceBuffer&) = delete;

    bool create(ID3D12Device* device);
    bool isValid() const;
    bool forwardReady() const;

    void     beginFrame(uint32_t frameIndex);
    uint32_t mark() const;
    void     rollback(uint32_t mark);
    bool     submit(const SkinnedPartSubmit& part);

    uint32_t partCount() const { return static_cast<uint32_t>(m_parts.size()); }
    uint32_t paletteCount() const;
    uint32_t gbufferBatchCount() const;
    uint32_t depthBatchCount() const;
    const GBufferBatch& gbufferBatch(uint32_t index) const;
    const DepthBatch&   depthBatch(uint32_t index) const;
    const SkinnedInstanceGpu* instances() const;
    const uint32_t*           gbufferIndices() const;
    const uint32_t*           depthIndices() const;

    // Mapped upload for one frame slot. Null until create() succeeds.
    const BonePaletteCB*     uploadedPalettes(uint32_t frameSlot) const;
    const SkinnedInstanceGpu* uploadedInstances(uint32_t frameSlot) const;

    void recordGBuffer(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, StaticPopulation population, const float viewProj[16], const float prevViewProj[16]) const;
    void recordDepth(ID3D12GraphicsCommandList* cmd, const float lightViewProj[16]) const;
    void recordForward(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, StaticPopulation population, const MeshForwardBatchConstants& lighting,
                       const float viewProj[16], uint64_t shadowCbv) const;

private:
    struct Part
    {
        const Mesh*      mesh       = nullptr;
        AssetID          material   = NULL_ASSET;
        Math::Matrix4f   world      = Math::Matrix4f::IDENTITY;
        Math::Matrix4f   prevWorld  = Math::Matrix4f::IDENTITY;
        float            color[4]{};
        StaticPopulation population = StaticPopulation::OpaqueModels;
        bool             alphaMask  = false;
        bool             castShadow = true;
        const AnimPose*  pose       = nullptr;
    };

    struct Gpu;

    void compile() const;
    void destroy();
    void rebuildPoseKeys();

    uint32_t                         m_frameIndex = 0;
    std::vector<Part>                m_parts;
    std::vector<const AnimPose*>     m_poseKeys;
    mutable bool                     m_compiled = false;
    mutable std::vector<SkinnedInstanceGpu> m_instances;
    mutable std::vector<BonePaletteCB>      m_palettes;
    mutable std::vector<uint32_t>           m_gbufferIndices;
    mutable std::vector<uint32_t>           m_depthIndices;
    mutable std::vector<GBufferBatch>       m_gbuffer;
    mutable std::vector<DepthBatch>         m_depth;
    std::unique_ptr<Gpu>             m_gpu;
};

} // namespace Dark
