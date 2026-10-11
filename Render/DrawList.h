#pragma once

#include "Render/RenderItem.h"
#include "Render/Frustum3f.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace Dark
{

class GpuModel;
class GpuResourceCache;
class InstanceCull;

void encodeCullAabb(const Math::AABox3f& box, bool castShadow, MeshCullAabb& out);
bool cullAabbVisible(const MeshCullAabb& aabb, const Math::Plane4f planes[6]);

enum class RenderPassKind : uint8_t
{
    GBuffer = 0,
    Depth,
};

struct ViewId
{
    uint8_t index = 0;
};

class DrawList
{
public:
    DrawList();
    ~DrawList();
    DrawList(const DrawList&) = delete;
    DrawList& operator=(const DrawList&) = delete;

    bool beginFrame(uint32_t frameIndex);

    // Upload heaps and the instanced PSOs. Failure leaves record calls as no-ops.
    bool create(ID3D12Device* device);
    bool gpuReady() const;
    void setIndirect(bool enabled);
    bool indirectReady() const;
    void setGpuCull(bool enabled);
    bool gpuCullActive() const { return m_gpuCull; }
    bool cullReady() const;
    bool dispatchCull(ID3D12GraphicsCommandList* cmd);
    ID3D12Resource* argumentResource(uint32_t frameSlot) const;
    ID3D12Resource* cullIndexResource(uint32_t frameSlot) const;

    // One primitive or editor prop. False when the cap is hit or the kind is not static.
    bool     submit(const RenderItem& item);
    uint32_t mark() const;
    void     rollback(uint32_t mark);

    // Every static opaque part, or none. Bakes localToRoot * entityWorld.
    // prevEntityWorld null means camera-only motion (the same entity matrix).
    // assetBounds is Model::bounds() in asset space. An invalid box is stored empty.
    bool submitStaticModel(GpuResourceCache& gpu, const GpuModel& model, const Math::Matrix4f& entityWorld, StaticPopulation population, bool castShadow, const Math::AABox3f& assetBounds, const Math::Matrix4f* prevEntityWorld = nullptr);

    // Culls with cullFrustum. Stores the draw matrices unchanged for the batch CB.
    void buildView(const Frustum3f& cullFrustum, const float drawViewProj[16], const float drawPrevViewProj[16], RenderPassKind pass, ViewId view);

    void recordOpaque(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, ViewId view, StaticPopulation population) const;
    void recordDepth(ID3D12GraphicsCommandList* cmd, ViewId view, const float lightViewProj[16]) const;

    // Opaque -forward color for one population. Uses the camera full-key batches.
    // Does not cull. A cull that already wrote this view is drawn with those args.
    bool forwardReady() const;
    void recordForward(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, ViewId view, StaticPopulation population,
                       const MeshForwardBatchConstants& lighting, uint64_t shadowCbv) const;

    uint32_t staticCount() const;
    uint32_t batchCount(ViewId view) const;
    uint32_t frameIndex() const { return m_frameIndex; }

    uint32_t            gbufferBatchCount() const;
    uint32_t            depthBatchCount() const;
    const GBufferBatch& gbufferBatch(uint32_t index) const;
    const DepthBatch&   depthBatch(uint32_t index) const;
    const RenderItem&   itemAt(uint32_t sortedIndex) const;

    uint32_t               visibleCount(ViewId view, uint32_t batchIndex) const;
    const DrawIndexedArgs& drawArg(ViewId view, uint32_t batchIndex) const;
    uint32_t               indexAt(ViewId view, uint32_t element) const;
    const float*           drawViewProj(ViewId view) const;
    const float*           drawPrevViewProj(ViewId view) const;

    // Current frame slot. Null until create() succeeds.
    const MeshInstanceGpu* uploadedInstances() const;
    const uint32_t*        uploadedIndices(ViewId view) const;

private:
    struct GpuUploads;
    struct ViewSlot
    {
        bool           built = false;
        bool           gpu   = false;
        RenderPassKind pass  = RenderPassKind::GBuffer;
        float          drawViewProj[16]{};
        float          drawPrevViewProj[16]{};
        float          planes[6][4]{};
        std::vector<uint32_t> indices;
        std::vector<uint32_t> visible;
        std::vector<DrawIndexedArgs> indirect;
    };

    void compile() const;
    void invalidateViews();
    bool acceptItem(const RenderItem& item);
    void uploadInstances() const;
    void uploadCullData() const;
    void uploadIndices(ViewId view) const;
    bool setupIndirect(ID3D12Device* device, GpuUploads& gpu);
    void setupCull(ID3D12Device* device, GpuUploads& gpu);
    bool copyViewArgs(ID3D12GraphicsCommandList* cmd, ViewId view) const;

    uint32_t m_frameIndex = 0;
    bool     m_indirect   = true;
    bool     m_gpuCull    = false;
    bool     m_cullDispatched = false;
    mutable std::array<bool, kMaxStaticViews> m_argsCopied{};
    std::unique_ptr<GpuUploads> m_gpu;
    std::unique_ptr<InstanceCull> m_cull;
    mutable bool m_compiled = false;
    mutable std::vector<RenderItem> m_items;
    mutable std::vector<uint32_t> m_itemGBuffer;
    mutable std::vector<uint32_t> m_itemDepth;
    mutable std::vector<GBufferBatch> m_gbuffer;
    mutable std::vector<DepthBatch> m_depth;
    std::array<ViewSlot, kMaxStaticViews> m_views{};
};

} // namespace Dark
