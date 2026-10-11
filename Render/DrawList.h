#pragma once

#include "Render/RenderItem.h"
#include "Render/Frustum3f.h"

#include <array>
#include <cstdint>
#include <vector>

struct ID3D12GraphicsCommandList;

namespace Dark
{

class GpuModel;
class GpuResourceCache;

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
    bool beginFrame(uint32_t frameIndex);

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

    uint32_t staticCount() const;
    uint32_t batchCount(ViewId view) const;
    uint32_t frameIndex() const { return m_frameIndex; }

    uint32_t            gbufferBatchCount() const;
    uint32_t            depthBatchCount() const;
    const GBufferBatch& gbufferBatch(uint32_t index) const;
    const DepthBatch&   depthBatch(uint32_t index) const;
    const RenderItem&   itemAt(uint32_t sortedIndex) const;

    uint32_t       visibleCount(ViewId view, uint32_t batchIndex) const;
    uint32_t       indexAt(ViewId view, uint32_t element) const;
    const float*   drawViewProj(ViewId view) const;
    const float*   drawPrevViewProj(ViewId view) const;

private:
    struct ViewSlot
    {
        bool           built = false;
        RenderPassKind pass  = RenderPassKind::GBuffer;
        float          drawViewProj[16]{};
        float          drawPrevViewProj[16]{};
        std::vector<uint32_t> indices;
        std::vector<uint32_t> visible;
    };

    void compile() const;
    void invalidateViews();
    bool acceptItem(const RenderItem& item);

    uint32_t m_frameIndex = 0;
    mutable bool m_compiled = false;
    mutable std::vector<RenderItem> m_items;
    mutable std::vector<GBufferBatch> m_gbuffer;
    mutable std::vector<DepthBatch> m_depth;
    std::array<ViewSlot, kMaxStaticViews> m_views{};
};

} // namespace Dark
