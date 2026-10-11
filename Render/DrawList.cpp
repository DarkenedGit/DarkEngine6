#include "Render/DrawList.h"

#include "Assets/Material.h"
#include "Core/Log.h"
#include "Render/GpuModel.h"
#include "Render/GpuResourceCache.h"
#include "Render/LocalShadowMath.h"
#include "Render/MaterialSurface.h"
#include "Render/Mesh.h"
#include "Render/ShadowCascades.h"

#include <algorithm>
#include <cstring>

namespace Dark
{

namespace
{

void logOnce(bool& logged, const char* message)
{
    if (logged)
        return;
    logged = true;
    DE_LOG_ERROR(LogCategory::Render, "{}", message);
}

void store16(float dst[16], const float* src)
{
    if (!src)
    {
        std::memset(dst, 0, 16 * sizeof(float));
        dst[0]  = 1.0f;
        dst[5]  = 1.0f;
        dst[10] = 1.0f;
        dst[15] = 1.0f;
        return;
    }
    std::memcpy(dst, src, 16 * sizeof(float));
}

bool containsKey(const std::vector<RenderItem>& items, const BatchKey& key)
{
    for (const RenderItem& item : items)
    {
        if (batchKeyEqual(batchKeyOf(item), key))
            return true;
    }
    return false;
}

uint32_t countDistinct(const std::vector<RenderItem>& items)
{
    uint32_t count = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const BatchKey key = batchKeyOf(items[i]);
        bool seen = false;
        for (size_t j = 0; j < i; ++j)
        {
            if (batchKeyEqual(batchKeyOf(items[j]), key))
            {
                seen = true;
                break;
            }
        }
        if (!seen)
            ++count;
    }
    return count;
}

} // namespace

static_assert(kMaxStaticViews == 1u + static_cast<uint32_t>(kMaxShadowCascades) + kLocalShadowSlicesPerFrame, "static views");

BatchKey batchKeyOf(const RenderItem& item)
{
    BatchKey key;
    key.population = static_cast<uint32_t>(item.population);
    key.meshKey    = item.mesh ? item.mesh->sortKey() : 0u;
    key.alphaMask  = item.alphaMask ? 1u : 0u;
    key.material   = item.material;
    return key;
}

float translucentViewZ(const Math::AABox3f& worldAabb, const Math::Matrix4f& view)
{
    if (!worldAabb.IsValid())
        return 0.0f;
    const Math::Vector4f viewPos = view * Math::Vector4f(worldAabb.Center(), 1.0f);
    return viewPos.z;
}

void sortTranslucent(TranslucentDraw* draws, uint32_t count, const Math::Matrix4f& view)
{
    if (!draws || count < 2)
        return;
    std::stable_sort(draws, draws + count, [&](const TranslucentDraw& a, const TranslucentDraw& b) {
        return translucentViewZ(a.worldAabb, view) > translucentViewZ(b.worldAabb, view);
    });
}

bool DrawList::beginFrame(uint32_t frameIndex)
{
    m_frameIndex = frameIndex;
    m_items.clear();
    m_items.reserve(256);
    m_gbuffer.clear();
    m_depth.clear();
    m_compiled = false;
    invalidateViews();
    return true;
}

bool DrawList::acceptItem(const RenderItem& item)
{
    static bool loggedItems  = false;
    static bool loggedBatches = false;
    if (m_items.size() >= kMaxStaticItems)
    {
        logOnce(loggedItems, "DrawList: static item cap reached");
        return false;
    }
    const BatchKey key = batchKeyOf(item);
    if (!containsKey(m_items, key) && countDistinct(m_items) >= kMaxBatches)
    {
        logOnce(loggedBatches, "DrawList: batch cap reached");
        return false;
    }
    return true;
}

bool DrawList::submit(const RenderItem& item)
{
    if (item.kind != RenderItemKind::Static || item.mesh == nullptr)
        return false;
    if (!acceptItem(item))
        return false;
    m_items.push_back(item);
    m_compiled = false;
    invalidateViews();
    return true;
}

uint32_t DrawList::mark() const
{
    return static_cast<uint32_t>(m_items.size());
}

void DrawList::rollback(uint32_t mark)
{
    if (mark > m_items.size())
        mark = static_cast<uint32_t>(m_items.size());
    m_items.resize(mark);
    m_compiled = false;
    invalidateViews();
}

bool DrawList::submitStaticModel(GpuResourceCache& gpu, const GpuModel& model, const Math::Matrix4f& entityWorld, StaticPopulation population, bool castShadow, const Math::AABox3f& assetBounds, const Math::Matrix4f* prevEntityWorld)
{
    const Math::Matrix4f& prevEntity = prevEntityWorld ? *prevEntityWorld : entityWorld;
    Math::AABox3f worldBox = Math::AABox3f::Empty();
    if (assetBounds.IsValid())
        worldBox = assetBounds.Transformed(entityWorld);

    const uint32_t saved = mark();
    const std::vector<GpuModel::Part>& parts = model.opaque();
    for (uint32_t i = 0; i < static_cast<uint32_t>(parts.size()); ++i)
    {
        const GpuModel::Part& part = parts[i];
        if (part.skinned || part.translucent)
            continue;

        RenderItem item;
        item.mesh         = &part.mesh;
        item.material     = part.materialId;
        item.world        = part.localToRoot * entityWorld;
        item.prevWorld    = part.localToRoot * prevEntity;
        item.worldAabb    = worldBox;
        item.kind         = RenderItemKind::Static;
        item.population   = population;
        item.castShadow   = castShadow;
        item.color[0]     = 1.0f;
        item.color[1]     = 1.0f;
        item.color[2]     = 1.0f;
        item.color[3]     = 0.0f;
        if (const AssetRef<Material> mat = gpu.cpuMaterial(part.materialId))
        {
            MeshGBufferConstants surface{};
            applyMaterialSurface(*mat, surface);
            item.color[0]  = surface.color[0];
            item.color[1]  = surface.color[1];
            item.color[2]  = surface.color[2];
            item.color[3]  = surface.color[3];
            item.alphaMask = surface.alphaModeMask > 0.5f;
        }
        if (!submit(item))
        {
            rollback(saved);
            return false;
        }
    }
    return true;
}

void DrawList::invalidateViews()
{
    for (ViewSlot& slot : m_views)
        slot = ViewSlot{};
}

void DrawList::compile() const
{
    if (m_compiled)
        return;

    std::stable_sort(m_items.begin(), m_items.end(), [](const RenderItem& a, const RenderItem& b) {
        return batchKeyLess(batchKeyOf(a), batchKeyOf(b));
    });

    m_gbuffer.clear();
    m_depth.clear();
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_items.size()); ++i)
    {
        const RenderItem& item = m_items[i];
        const BatchKey key = batchKeyOf(item);
        if (m_gbuffer.empty() || !batchKeyEqual(m_gbuffer.back().key, key))
        {
            GBufferBatch batch;
            batch.key       = key;
            batch.itemBegin = i;
            batch.itemCount = 1;
            batch.mesh      = item.mesh;
            m_gbuffer.push_back(batch);
        }
        else
        {
            m_gbuffer.back().itemCount += 1;
        }

        const bool newDepth = m_depth.empty() || m_depth.back().population != key.population || m_depth.back().meshKey != key.meshKey;
        if (newDepth)
        {
            DepthBatch batch;
            batch.population = key.population;
            batch.meshKey    = key.meshKey;
            batch.itemBegin  = i;
            batch.itemCount  = 1;
            batch.mesh       = item.mesh;
            m_depth.push_back(batch);
        }
        else
        {
            m_depth.back().itemCount += 1;
        }
    }
    m_compiled = true;
}

void DrawList::buildView(const Frustum3f& cullFrustum, const float drawViewProj[16], const float drawPrevViewProj[16], RenderPassKind pass, ViewId view)
{
    static bool loggedView = false;
    if (view.index >= kMaxStaticViews)
    {
        logOnce(loggedView, "DrawList: static view cap reached");
        return;
    }

    compile();
    ViewSlot& slot = m_views[view.index];
    slot           = ViewSlot{};
    slot.built     = true;
    slot.pass      = pass;
    store16(slot.drawViewProj, drawViewProj);
    store16(slot.drawPrevViewProj, drawPrevViewProj);

    const uint32_t batches = (pass == RenderPassKind::Depth) ? static_cast<uint32_t>(m_depth.size()) : static_cast<uint32_t>(m_gbuffer.size());
    slot.indices.assign(kMaxStaticItems, 0u);
    slot.visible.assign(batches, 0u);

    for (uint32_t batchIndex = 0; batchIndex < batches; ++batchIndex)
    {
        const uint32_t begin = (pass == RenderPassKind::Depth) ? m_depth[batchIndex].itemBegin : m_gbuffer[batchIndex].itemBegin;
        const uint32_t count = (pass == RenderPassKind::Depth) ? m_depth[batchIndex].itemCount : m_gbuffer[batchIndex].itemCount;
        uint32_t written = 0;
        for (uint32_t n = 0; n < count; ++n)
        {
            const uint32_t itemIndex = begin + n;
            const RenderItem& item = m_items[itemIndex];
            if (pass == RenderPassKind::Depth && !item.castShadow)
                continue;
            if (item.worldAabb.IsValid() && !cullFrustum.Intersects(item.worldAabb))
                continue;
            if (written >= count)
                continue;
            slot.indices[begin + written] = itemIndex;
            ++written;
        }
        slot.visible[batchIndex] = written;
    }
}

void DrawList::recordOpaque(ID3D12GraphicsCommandList*, GpuResourceCache&, ViewId, StaticPopulation) const
{
}

void DrawList::recordDepth(ID3D12GraphicsCommandList*, ViewId, const float*) const
{
}

uint32_t DrawList::staticCount() const
{
    return static_cast<uint32_t>(m_items.size());
}

uint32_t DrawList::batchCount(ViewId view) const
{
    if (view.index >= kMaxStaticViews || !m_views[view.index].built)
        return 0;
    return static_cast<uint32_t>(m_views[view.index].visible.size());
}

uint32_t DrawList::gbufferBatchCount() const
{
    compile();
    return static_cast<uint32_t>(m_gbuffer.size());
}

uint32_t DrawList::depthBatchCount() const
{
    compile();
    return static_cast<uint32_t>(m_depth.size());
}

const GBufferBatch& DrawList::gbufferBatch(uint32_t index) const
{
    compile();
    static const GBufferBatch kEmpty{};
    if (index >= m_gbuffer.size())
        return kEmpty;
    return m_gbuffer[index];
}

const DepthBatch& DrawList::depthBatch(uint32_t index) const
{
    compile();
    static const DepthBatch kEmpty{};
    if (index >= m_depth.size())
        return kEmpty;
    return m_depth[index];
}

const RenderItem& DrawList::itemAt(uint32_t sortedIndex) const
{
    compile();
    static const RenderItem kEmpty{};
    if (sortedIndex >= m_items.size())
        return kEmpty;
    return m_items[sortedIndex];
}

uint32_t DrawList::visibleCount(ViewId view, uint32_t batchIndex) const
{
    if (view.index >= kMaxStaticViews || !m_views[view.index].built)
        return 0;
    const std::vector<uint32_t>& visible = m_views[view.index].visible;
    if (batchIndex >= visible.size())
        return 0;
    return visible[batchIndex];
}

uint32_t DrawList::indexAt(ViewId view, uint32_t element) const
{
    if (view.index >= kMaxStaticViews || !m_views[view.index].built)
        return 0;
    const std::vector<uint32_t>& indices = m_views[view.index].indices;
    if (element >= indices.size())
        return 0;
    return indices[element];
}

const float* DrawList::drawViewProj(ViewId view) const
{
    if (view.index >= kMaxStaticViews || !m_views[view.index].built)
        return nullptr;
    return m_views[view.index].drawViewProj;
}

const float* DrawList::drawPrevViewProj(ViewId view) const
{
    if (view.index >= kMaxStaticViews || !m_views[view.index].built)
        return nullptr;
    return m_views[view.index].drawPrevViewProj;
}

} // namespace Dark
