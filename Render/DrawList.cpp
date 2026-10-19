#include "Render/DrawList.h"

#include "Assets/Material.h"
#include "Core/Log.h"
#include "Render/GpuModel.h"
#include "Render/GpuResourceCache.h"
#include "Render/InstanceCull.h"
#include "Render/InstancedMeshPipeline.h"
#include "Render/LocalShadowMath.h"
#include "Render/MaterialSurface.h"
#include "Render/Mesh.h"
#include "Render/MeshConstants.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShadowCascades.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <d3d12.h>
#include <wrl/client.h>

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

constexpr uint32_t kUploadSlots = 2;
constexpr UINT64 kCullAabbBytes   = static_cast<UINT64>(kMaxStaticItems) * sizeof(MeshCullAabb);
constexpr UINT64 kCullRecordBytes = static_cast<UINT64>(kMaxBatches) * sizeof(GpuBatchRecord);
constexpr UINT64 kCullSlotBytes   = kCullAabbBytes + kCullRecordBytes * 2;

bool FailedHr(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr))
        return false;
    DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

ComPtr<ID3D12Resource> createUpload(ID3D12Device* device, UINT64 bytes, const char* what)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = bytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)), what))
        return nullptr;
    return res;
}

ComPtr<ID3D12Resource> createDefault(ID3D12Device* device, UINT64 bytes, const char* what)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = bytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> res;
    if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&res)), what))
        return nullptr;
    return res;
}

void transitionBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (!cmd || !resource || before == after)
        return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
}

void drawStaticBatch(const Mesh& mesh, ID3D12GraphicsCommandList* cmd, bool indirect, ID3D12CommandSignature* signature, ID3D12Resource* args, UINT64 argOffset, uint32_t visible)
{
    if (indirect)
        mesh.drawIndirect(cmd, signature, args, argOffset);
    else
        mesh.drawInstanced(cmd, visible);
}

void fillForwardScalars(GpuResourceCache& gpu, AssetID material, MeshForwardBatchConstants& cb)
{
    MeshGBufferConstants surface{};
    if (const AssetRef<Material> mat = gpu.cpuMaterial(material))
        applyMaterialSurface(*mat, surface);
    else
    {
        surface.ao            = 1.0f;
        surface.normalScale   = 1.0f;
        surface.alphaCutoff   = 0.5f;
        surface.alphaModeMask = 0.0f;
    }
    cb.normalScale   = surface.normalScale;
    cb.ao            = surface.ao;
    cb.alphaCutoff   = surface.alphaCutoff;
    cb.alphaModeMask = surface.alphaModeMask;
}

void fillBatchScalars(GpuResourceCache& gpu, AssetID material, MeshBatchConstants& cb)
{
    MeshGBufferConstants surface{};
    if (const AssetRef<Material> mat = gpu.cpuMaterial(material))
        applyMaterialSurface(*mat, surface);
    else
    {
        surface.roughness     = 1.0f;
        surface.metallic      = 0.0f;
        surface.ao            = 1.0f;
        surface.normalScale   = 1.0f;
        surface.alphaCutoff   = 0.5f;
        surface.alphaModeMask = 0.0f;
    }
    cb.roughness     = surface.roughness;
    cb.metallic      = surface.metallic;
    cb.ao            = surface.ao;
    cb.normalScale   = surface.normalScale;
    cb.alphaCutoff   = surface.alphaCutoff;
    cb.alphaModeMask = surface.alphaModeMask;
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
static_assert(kUploadSlots == Renderer::kFrameCount, "draw list upload slots");
static_assert(sizeof(DrawIndexedArgs) == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), "indirect arg size");
static_assert(offsetof(DrawIndexedArgs, indexCountPerInstance) == offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, IndexCountPerInstance), "index count");
static_assert(offsetof(DrawIndexedArgs, instanceCount) == offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, InstanceCount), "instance count");
static_assert(offsetof(DrawIndexedArgs, startIndexLocation) == offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, StartIndexLocation), "start index");
static_assert(offsetof(DrawIndexedArgs, baseVertexLocation) == offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, BaseVertexLocation), "base vertex");
static_assert(offsetof(DrawIndexedArgs, startInstanceLocation) == offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, StartInstanceLocation), "start instance");
static_assert(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) % 4u == 0u, "indirect stride");
static_assert(offsetof(MeshCullAabb, flags) == 12, "cull flags");
static_assert(offsetof(MeshCullAabb, gbufferBatch) == 28, "cull gbuffer batch");
static_assert(offsetof(MeshCullAabb, depthBatch) == 32, "cull depth batch");
static_assert(offsetof(GpuBatchRecord, itemBegin) == 4, "batch begin");

struct DrawList::GpuUploads
{
    InstancedMeshPipeline pipeline;
    ComPtr<ID3D12Resource> instances;
    ComPtr<ID3D12Resource> indices;
    uint8_t* instanceBytes = nullptr;
    uint8_t* indexBytes    = nullptr;

    // One buffer per frame slot. A buffer has one state, and the other slot can still be queued.
    ComPtr<ID3D12CommandSignature> signature;
    ComPtr<ID3D12Resource> args[kUploadSlots];
    ComPtr<ID3D12Resource> argStaging;
    uint8_t* argStagingBytes = nullptr;
    ComPtr<ID3D12Resource> cullUpload;
    uint8_t* cullBytes = nullptr;
    mutable D3D12_RESOURCE_STATES argState[kUploadSlots] = {
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_DEST,
    };

    ~GpuUploads()
    {
        if (instances && instanceBytes)
            instances->Unmap(0, nullptr);
        if (indices && indexBytes)
            indices->Unmap(0, nullptr);
        if (argStaging && argStagingBytes)
            argStaging->Unmap(0, nullptr);
        if (cullUpload && cullBytes)
            cullUpload->Unmap(0, nullptr);
    }
};

DrawList::DrawList() = default;
DrawList::~DrawList() = default;

bool DrawList::create(ID3D12Device* device)
{
    m_cull.reset();
    m_gpuCull        = false;
    m_cullDispatched = false;
    m_gpu.reset();
    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "DrawList::create: null device");
        return false;
    }

    std::unique_ptr<GpuUploads> gpu = std::make_unique<GpuUploads>();
    if (!gpu->pipeline.create(device))
        return false;

    const UINT64 instanceBytes = static_cast<UINT64>(kUploadSlots) * kMaxStaticItems * sizeof(MeshInstanceGpu);
    const UINT64 indexBytes    = static_cast<UINT64>(kUploadSlots) * kMaxStaticViews * kMaxStaticItems * sizeof(uint32_t);
    gpu->instances = createUpload(device, instanceBytes, "CreateCommittedResource (draw list instances)");
    gpu->indices   = createUpload(device, indexBytes, "CreateCommittedResource (draw list indices)");
    if (!gpu->instances || !gpu->indices)
        return false;
    if (FailedHr(gpu->instances->Map(0, nullptr, reinterpret_cast<void**>(&gpu->instanceBytes)), "Map draw list instances")
        || FailedHr(gpu->indices->Map(0, nullptr, reinterpret_cast<void**>(&gpu->indexBytes)), "Map draw list indices"))
    {
        return false;
    }
    gpu->instances->SetName(L"DrawListInstances");
    gpu->indices->SetName(L"DrawListIndices");
    if (!setupIndirect(device, *gpu))
        DE_LOG_ERROR(LogCategory::Render, "DrawList: indirect arguments unavailable; static draws stay DrawIndexedInstanced");
    else
        setupCull(device, *gpu);
    m_gpu = std::move(gpu);
    m_argsCopied.fill(false);
    if (m_compiled)
    {
        uploadInstances();
        uploadCullData();
    }
    for (uint32_t i = 0; i < kMaxStaticViews; ++i)
    {
        if (m_views[i].built)
            uploadIndices(ViewId{ static_cast<uint8_t>(i) });
    }
    return true;
}

bool DrawList::gpuReady() const
{
    return m_gpu && m_gpu->pipeline.isValid() && m_gpu->instanceBytes && m_gpu->indexBytes;
}

void DrawList::uploadInstances() const
{
    if (!m_gpu || !m_gpu->instanceBytes)
        return;
    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    MeshInstanceGpu* dst = reinterpret_cast<MeshInstanceGpu*>(m_gpu->instanceBytes) + static_cast<size_t>(frameSlot) * kMaxStaticItems;
    const uint32_t count = static_cast<uint32_t>(m_items.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        const RenderItem& item = m_items[i];
        std::memcpy(dst[i].world, item.world.m_afEntry, sizeof(dst[i].world));
        std::memcpy(dst[i].prevWorld, item.prevWorld.m_afEntry, sizeof(dst[i].prevWorld));
        std::memcpy(dst[i].color, item.color, sizeof(dst[i].color));
    }
}

void DrawList::uploadCullData() const
{
    if (!m_gpu || !m_gpu->cullBytes)
        return;
    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    uint8_t* base            = m_gpu->cullBytes + static_cast<size_t>(frameSlot) * static_cast<size_t>(kCullSlotBytes);
    MeshCullAabb* aabbs      = reinterpret_cast<MeshCullAabb*>(base);
    GpuBatchRecord* gbuffer  = reinterpret_cast<GpuBatchRecord*>(base + kCullAabbBytes);
    GpuBatchRecord* depth    = reinterpret_cast<GpuBatchRecord*>(base + kCullAabbBytes + kCullRecordBytes);

    const uint32_t count = static_cast<uint32_t>(m_items.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        encodeCullAabb(m_items[i].worldAabb, m_items[i].castShadow, aabbs[i]);
        aabbs[i].gbufferBatch = i < m_itemGBuffer.size() ? m_itemGBuffer[i] : 0u;
        aabbs[i].depthBatch   = i < m_itemDepth.size() ? m_itemDepth[i] : 0u;
    }
    const uint32_t gbufferCount = static_cast<uint32_t>(m_gbuffer.size());
    for (uint32_t i = 0; i < gbufferCount && i < kMaxBatches; ++i)
    {
        const GBufferBatch& batch = m_gbuffer[i];
        gbuffer[i].indexCount     = batch.mesh ? batch.mesh->indexCount() : 0u;
        gbuffer[i].itemBegin      = batch.itemBegin;
        gbuffer[i].itemCount      = batch.itemCount;
        gbuffer[i]._pad           = 0;
    }
    const uint32_t depthCount = static_cast<uint32_t>(m_depth.size());
    for (uint32_t i = 0; i < depthCount && i < kMaxBatches; ++i)
    {
        const DepthBatch& batch = m_depth[i];
        depth[i].indexCount     = batch.mesh ? batch.mesh->indexCount() : 0u;
        depth[i].itemBegin      = batch.itemBegin;
        depth[i].itemCount      = batch.itemCount;
        depth[i]._pad           = 0;
    }
}

void DrawList::uploadIndices(ViewId view) const
{
    if (!m_gpu || !m_gpu->indexBytes || view.index >= kMaxStaticViews)
        return;
    const ViewSlot& slot = m_views[view.index];
    if (!slot.built || slot.indices.size() < kMaxStaticItems)
        return;
    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    uint32_t* dst = reinterpret_cast<uint32_t*>(m_gpu->indexBytes) + (static_cast<size_t>(frameSlot) * kMaxStaticViews + view.index) * kMaxStaticItems;
    std::memcpy(dst, slot.indices.data(), kMaxStaticItems * sizeof(uint32_t));
}

const MeshInstanceGpu* DrawList::uploadedInstances() const
{
    if (!m_gpu || !m_gpu->instanceBytes)
        return nullptr;
    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    return reinterpret_cast<const MeshInstanceGpu*>(m_gpu->instanceBytes) + static_cast<size_t>(frameSlot) * kMaxStaticItems;
}

const uint32_t* DrawList::uploadedIndices(ViewId view) const
{
    if (!m_gpu || !m_gpu->indexBytes || view.index >= kMaxStaticViews)
        return nullptr;
    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    return reinterpret_cast<const uint32_t*>(m_gpu->indexBytes) + (static_cast<size_t>(frameSlot) * kMaxStaticViews + view.index) * kMaxStaticItems;
}

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

void encodeCullAabb(const Math::AABox3f& box, bool castShadow, MeshCullAabb& out)
{
    // Empty() is Min=+inf, Max=-inf. Center() of that box is NaN, so it is flagged instead of measured.
    out = MeshCullAabb{};
    if (box.IsValid())
    {
        const Math::Vector3f center = box.Center();
        const Math::Vector3f extent = box.Extents();
        out.center[0]                = center.x;
        out.center[1]                = center.y;
        out.center[2]                = center.z;
        out.extent[0]                = extent.x;
        out.extent[1]                = extent.y;
        out.extent[2]                = extent.z;
    }
    else
    {
        out.flags |= kCullAlwaysVisible;
    }
    if (castShadow)
        out.flags |= kCullCastShadow;
}

bool cullAabbVisible(const MeshCullAabb& aabb, const Math::Plane4f planes[6])
{
    if ((aabb.flags & kCullAlwaysVisible) != 0)
        return true;
    if (!planes)
        return false;
    for (int i = 0; i < 6; ++i)
    {
        const Math::Plane4f& plane = planes[i];
        const float px             = plane.x >= 0.0f ? aabb.center[0] + aabb.extent[0] : aabb.center[0] - aabb.extent[0];
        const float py             = plane.y >= 0.0f ? aabb.center[1] + aabb.extent[1] : aabb.center[1] - aabb.extent[1];
        const float pz             = plane.z >= 0.0f ? aabb.center[2] + aabb.extent[2] : aabb.center[2] - aabb.extent[2];
        if (plane.x * px + plane.y * py + plane.z * pz + plane.w < 0.0f)
            return false;
    }
    return true;
}

bool DrawList::beginFrame(uint32_t frameIndex)
{
    m_frameIndex     = frameIndex;
    m_gpuCull        = false;
    m_cullDispatched = false;
    m_items.clear();
    m_items.reserve(256);
    m_itemGBuffer.clear();
    m_itemDepth.clear();
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
    m_argsCopied.fill(false);
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
    m_itemGBuffer.assign(m_items.size(), 0u);
    m_itemDepth.assign(m_items.size(), 0u);
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
        m_itemGBuffer[i] = static_cast<uint32_t>(m_gbuffer.size() - 1);

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
        m_itemDepth[i] = static_cast<uint32_t>(m_depth.size() - 1);
    }
    m_compiled = true;
    uploadInstances();
    uploadCullData();
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
    if (m_gpuCull)
    {
        slot.gpu = true;
        for (int plane = 0; plane < Frustum3f::Count; ++plane)
        {
            const Math::Plane4f& frustumPlane = cullFrustum.Planes[static_cast<size_t>(plane)];
            slot.planes[plane][0]             = frustumPlane.x;
            slot.planes[plane][1]             = frustumPlane.y;
            slot.planes[plane][2]             = frustumPlane.z;
            slot.planes[plane][3]             = frustumPlane.w;
        }
        m_argsCopied[view.index] = false;
        return;
    }

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

    slot.indirect.assign(batches, DrawIndexedArgs{});
    for (uint32_t batchIndex = 0; batchIndex < batches; ++batchIndex)
    {
        const Mesh* mesh = (pass == RenderPassKind::Depth) ? m_depth[batchIndex].mesh : m_gbuffer[batchIndex].mesh;
        DrawIndexedArgs& args = slot.indirect[batchIndex];
        args.indexCountPerInstance = mesh ? mesh->indexCount() : 0u;
        args.instanceCount         = slot.visible[batchIndex];
    }
    m_argsCopied[view.index] = false;
    uploadIndices(view);
}

void DrawList::recordOpaque(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, ViewId view, StaticPopulation population) const
{
    if (!cmd || !gpuReady())
        return;
    compile();
    if (view.index >= kMaxStaticViews)
        return;
    const ViewSlot& slot = m_views[view.index];
    if (!slot.built || slot.pass != RenderPassKind::GBuffer)
        return;

    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    const D3D12_GPU_VIRTUAL_ADDRESS instanceGpu = m_gpu->instances->GetGPUVirtualAddress() + static_cast<UINT64>(frameSlot) * kMaxStaticItems * sizeof(MeshInstanceGpu);
    if (slot.gpu)
    {
        // copyViewArgs would put the argument buffer back in COPY_DEST and overwrite the GPU counts.
        if (!m_cullDispatched || !m_cull || !indirectReady())
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS indexGpu = m_cull->indexAddress(frameSlot, view.index);
        if (indexGpu == 0)
            return;
        const uint32_t populationId = static_cast<uint32_t>(population);
        bool any = false;
        for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
        {
            if (m_gbuffer[i].key.population == populationId && m_gbuffer[i].mesh != nullptr)
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        m_gpu->pipeline.bindGBuffer(cmd);
        m_gpu->pipeline.setGBufferBuffers(cmd, instanceGpu, indexGpu);
        for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
        {
            const GBufferBatch& batch = m_gbuffer[i];
            if (batch.key.population != populationId || batch.mesh == nullptr)
                continue;
            MeshBatchConstants cb{};
            std::memcpy(cb.viewProj, slot.drawViewProj, sizeof(cb.viewProj));
            std::memcpy(cb.prevViewProj, slot.drawPrevViewProj, sizeof(cb.prevViewProj));
            fillBatchScalars(gpu, batch.key.material, cb);
            cb.baseIndex = batch.itemBegin;
            m_gpu->pipeline.setGBufferConstants(cmd, cb);
            gpu.bindMaterial(cmd, batch.key.material, InstancedMeshPipeline::kGbMaterial);
            const UINT64 argOffset = (static_cast<UINT64>(view.index) * kMaxBatches + i) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
            batch.mesh->drawIndirect(cmd, m_gpu->signature.Get(), m_gpu->args[frameSlot].Get(), argOffset);
        }
        return;
    }

    const uint32_t populationId = static_cast<uint32_t>(population);
    bool any = false;
    for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
    {
        if (m_gbuffer[i].key.population != populationId)
            continue;
        if (i >= slot.visible.size() || slot.visible[i] == 0 || m_gbuffer[i].mesh == nullptr)
            continue;
        any = true;
        break;
    }
    if (!any)
        return;

    const bool useIndirect   = m_indirect && indirectReady() && copyViewArgs(cmd, view);
    const D3D12_GPU_VIRTUAL_ADDRESS indexGpu = m_gpu->indices->GetGPUVirtualAddress()
        + (static_cast<UINT64>(frameSlot) * kMaxStaticViews + view.index) * kMaxStaticItems * sizeof(uint32_t);
    m_gpu->pipeline.bindGBuffer(cmd);
    m_gpu->pipeline.setGBufferBuffers(cmd, instanceGpu, indexGpu);

    for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
    {
        const GBufferBatch& batch = m_gbuffer[i];
        if (batch.key.population != populationId || i >= slot.visible.size() || slot.visible[i] == 0 || batch.mesh == nullptr)
            continue;
        MeshBatchConstants cb{};
        std::memcpy(cb.viewProj, slot.drawViewProj, sizeof(cb.viewProj));
        std::memcpy(cb.prevViewProj, slot.drawPrevViewProj, sizeof(cb.prevViewProj));
        fillBatchScalars(gpu, batch.key.material, cb);
        cb.baseIndex = batch.itemBegin;
        m_gpu->pipeline.setGBufferConstants(cmd, cb);
        gpu.bindMaterial(cmd, batch.key.material, InstancedMeshPipeline::kGbMaterial);
        const UINT64 argOffset = (static_cast<UINT64>(view.index) * kMaxBatches + i) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        drawStaticBatch(*batch.mesh, cmd, useIndirect, m_gpu->signature.Get(), m_gpu->args[frameSlot].Get(), argOffset, slot.visible[i]);
    }
}

bool DrawList::forwardReady() const
{
    return m_gpu && m_gpu->pipeline.forwardReady();
}

void DrawList::recordForward(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, ViewId view, StaticPopulation population,
                             const MeshForwardBatchConstants& lighting, uint64_t shadowCbv) const
{
    if (!cmd || !gpuReady() || !forwardReady())
        return;
    compile();
    if (view.index >= kMaxStaticViews)
        return;
    const ViewSlot& slot = m_views[view.index];
    if (!slot.built || slot.pass != RenderPassKind::GBuffer)
        return;

    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    const D3D12_GPU_VIRTUAL_ADDRESS instanceGpu = m_gpu->instances->GetGPUVirtualAddress() + static_cast<UINT64>(frameSlot) * kMaxStaticItems * sizeof(MeshInstanceGpu);
    const uint32_t populationId = static_cast<uint32_t>(population);

    auto issue = [&](const GBufferBatch& batch, uint32_t batchIndex, bool indirect, uint32_t visible) {
        MeshForwardBatchConstants cb = lighting;
        std::memcpy(cb.viewProj, slot.drawViewProj, sizeof(cb.viewProj));
        fillForwardScalars(gpu, batch.key.material, cb);
        cb.baseIndex = batch.itemBegin;
        cb.pad[0]    = 0;
        cb.pad[1]    = 0;
        cb.pad[2]    = 0;
        m_gpu->pipeline.setForwardConstants(cmd, cb);
        gpu.bindMaterial(cmd, batch.key.material, InstancedMeshPipeline::kForwardMaterial);
        const UINT64 argOffset = (static_cast<UINT64>(view.index) * kMaxBatches + batchIndex) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        if (indirect)
            batch.mesh->drawIndirect(cmd, m_gpu->signature.Get(), m_gpu->args[frameSlot].Get(), argOffset);
        else
            batch.mesh->drawInstanced(cmd, visible);
    };

    if (slot.gpu)
    {
        if (!m_cullDispatched || !m_cull || !indirectReady())
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS indexGpu = m_cull->indexAddress(frameSlot, view.index);
        if (indexGpu == 0)
            return;
        bool any = false;
        for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
        {
            if (m_gbuffer[i].key.population == populationId && m_gbuffer[i].mesh != nullptr)
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        m_gpu->pipeline.bindForward(cmd);
        m_gpu->pipeline.setForwardBuffers(cmd, instanceGpu, indexGpu);
        m_gpu->pipeline.setForwardShadow(cmd, static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(shadowCbv));
        for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
        {
            const GBufferBatch& batch = m_gbuffer[i];
            if (batch.key.population != populationId || batch.mesh == nullptr)
                continue;
            issue(batch, i, true, 0);
        }
        return;
    }

    bool any = false;
    for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
    {
        if (m_gbuffer[i].key.population != populationId)
            continue;
        if (i >= slot.visible.size() || slot.visible[i] == 0 || m_gbuffer[i].mesh == nullptr)
            continue;
        any = true;
        break;
    }
    if (!any)
        return;

    const bool useIndirect = m_indirect && indirectReady() && copyViewArgs(cmd, view);
    const D3D12_GPU_VIRTUAL_ADDRESS indexGpu = m_gpu->indices->GetGPUVirtualAddress()
        + (static_cast<UINT64>(frameSlot) * kMaxStaticViews + view.index) * kMaxStaticItems * sizeof(uint32_t);
    m_gpu->pipeline.bindForward(cmd);
    m_gpu->pipeline.setForwardBuffers(cmd, instanceGpu, indexGpu);
    m_gpu->pipeline.setForwardShadow(cmd, static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(shadowCbv));
    for (uint32_t i = 0; i < m_gbuffer.size(); ++i)
    {
        const GBufferBatch& batch = m_gbuffer[i];
        if (batch.key.population != populationId || i >= slot.visible.size() || slot.visible[i] == 0 || batch.mesh == nullptr)
            continue;
        issue(batch, i, useIndirect, slot.visible[i]);
    }
}

void DrawList::recordDepth(ID3D12GraphicsCommandList* cmd, ViewId view, const float lightViewProj[16]) const
{
    if (!cmd || !gpuReady())
        return;
    compile();
    if (view.index >= kMaxStaticViews)
        return;
    const ViewSlot& slot = m_views[view.index];
    if (!slot.built || slot.pass != RenderPassKind::Depth)
        return;

    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    const D3D12_GPU_VIRTUAL_ADDRESS instanceGpu = m_gpu->instances->GetGPUVirtualAddress() + static_cast<UINT64>(frameSlot) * kMaxStaticItems * sizeof(MeshInstanceGpu);
    if (slot.gpu)
    {
        if (!m_cullDispatched || !m_cull || !indirectReady())
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS indexGpu = m_cull->indexAddress(frameSlot, view.index);
        if (indexGpu == 0)
            return;
        bool any = false;
        for (uint32_t i = 0; i < m_depth.size(); ++i)
        {
            if (m_depth[i].mesh != nullptr)
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        m_gpu->pipeline.bindDepth(cmd);
        m_gpu->pipeline.setDepthBuffers(cmd, instanceGpu, indexGpu);
        float light[16];
        store16(light, lightViewProj);
        for (uint32_t i = 0; i < m_depth.size(); ++i)
        {
            const DepthBatch& batch = m_depth[i];
            if (batch.mesh == nullptr)
                continue;
            MeshDepthBatchConstants cb{};
            std::memcpy(cb.lightViewProj, light, sizeof(cb.lightViewProj));
            cb.baseIndex = batch.itemBegin;
            m_gpu->pipeline.setDepthConstants(cmd, cb);
            const UINT64 argOffset = (static_cast<UINT64>(view.index) * kMaxBatches + i) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
            batch.mesh->drawIndirect(cmd, m_gpu->signature.Get(), m_gpu->args[frameSlot].Get(), argOffset);
        }
        return;
    }

    bool any = false;
    for (uint32_t i = 0; i < m_depth.size(); ++i)
    {
        if (i >= slot.visible.size() || slot.visible[i] == 0 || m_depth[i].mesh == nullptr)
            continue;
        any = true;
        break;
    }
    if (!any)
        return;

    const bool useIndirect   = m_indirect && indirectReady() && copyViewArgs(cmd, view);
    const D3D12_GPU_VIRTUAL_ADDRESS indexGpu = m_gpu->indices->GetGPUVirtualAddress()
        + (static_cast<UINT64>(frameSlot) * kMaxStaticViews + view.index) * kMaxStaticItems * sizeof(uint32_t);
    m_gpu->pipeline.bindDepth(cmd);
    m_gpu->pipeline.setDepthBuffers(cmd, instanceGpu, indexGpu);

    float light[16];
    store16(light, lightViewProj);
    for (uint32_t i = 0; i < m_depth.size(); ++i)
    {
        const DepthBatch& batch = m_depth[i];
        if (i >= slot.visible.size() || slot.visible[i] == 0 || batch.mesh == nullptr)
            continue;
        MeshDepthBatchConstants cb{};
        std::memcpy(cb.lightViewProj, light, sizeof(cb.lightViewProj));
        cb.baseIndex = batch.itemBegin;
        m_gpu->pipeline.setDepthConstants(cmd, cb);
        const UINT64 argOffset = (static_cast<UINT64>(view.index) * kMaxBatches + i) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        drawStaticBatch(*batch.mesh, cmd, useIndirect, m_gpu->signature.Get(), m_gpu->args[frameSlot].Get(), argOffset, slot.visible[i]);
    }
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

void DrawList::setIndirect(bool enabled)
{
    m_indirect = enabled;
}

void DrawList::setThermal(bool on)
{
    if (m_gpu)
        m_gpu->pipeline.setThermal(on);
}

bool DrawList::indirectReady() const
{
    return m_gpu && m_gpu->signature && m_gpu->args[0] && m_gpu->args[1] && m_gpu->argStagingBytes;
}

const DrawIndexedArgs& DrawList::drawArg(ViewId view, uint32_t batchIndex) const
{
    static const DrawIndexedArgs kEmpty{};
    if (view.index >= kMaxStaticViews || !m_views[view.index].built)
        return kEmpty;
    const std::vector<DrawIndexedArgs>& args = m_views[view.index].indirect;
    if (batchIndex >= args.size())
        return kEmpty;
    return args[batchIndex];
}

bool DrawList::setupIndirect(ID3D12Device* device, GpuUploads& gpu)
{
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride       = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs   = &argument;

    if (FailedHr(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&gpu.signature)), "CreateCommandSignature (draw list)"))
    {
        gpu.signature.Reset();
        return false;
    }

    const UINT64 slotBytes = static_cast<UINT64>(kMaxStaticViews) * kMaxBatches * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    gpu.args[0]    = createDefault(device, slotBytes, "CreateCommittedResource (draw list indirect 0)");
    gpu.args[1]    = createDefault(device, slotBytes, "CreateCommittedResource (draw list indirect 1)");
    gpu.argStaging = createUpload(device, slotBytes * kUploadSlots, "CreateCommittedResource (draw list indirect staging)");
    if (!gpu.args[0] || !gpu.args[1] || !gpu.argStaging
        || FailedHr(gpu.argStaging->Map(0, nullptr, reinterpret_cast<void**>(&gpu.argStagingBytes)), "Map draw list indirect staging"))
    {
        gpu.signature.Reset();
        gpu.args[0].Reset();
        gpu.args[1].Reset();
        if (gpu.argStaging && gpu.argStagingBytes)
        {
            gpu.argStaging->Unmap(0, nullptr);
            gpu.argStagingBytes = nullptr;
        }
        gpu.argStaging.Reset();
        return false;
    }

    gpu.args[0]->SetName(L"DrawListIndirectArgs0");
    gpu.args[1]->SetName(L"DrawListIndirectArgs1");
    gpu.argStaging->SetName(L"DrawListIndirectStaging");
    gpu.argState[0] = D3D12_RESOURCE_STATE_COPY_DEST;
    gpu.argState[1] = D3D12_RESOURCE_STATE_COPY_DEST;
    return true;
}

bool DrawList::copyViewArgs(ID3D12GraphicsCommandList* cmd, ViewId view) const
{
    if (!cmd || !m_gpu || view.index >= kMaxStaticViews)
        return false;
    if (m_argsCopied[view.index])
        return true;

    const ViewSlot& slot = m_views[view.index];
    if (slot.indirect.empty() || slot.indirect.size() > kMaxBatches)
        return false;

    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    const UINT64 argBytes    = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    const UINT64 viewBytes   = static_cast<UINT64>(kMaxBatches) * argBytes;
    const UINT64 halfBytes   = static_cast<UINT64>(kMaxStaticViews) * viewBytes;
    const UINT64 dstOffset   = static_cast<UINT64>(view.index) * viewBytes;
    const UINT64 srcOffset   = static_cast<UINT64>(frameSlot) * halfBytes + dstOffset;
    const UINT64 copyBytes   = static_cast<UINT64>(slot.indirect.size()) * argBytes;

    uint8_t* staging = m_gpu->argStagingBytes + srcOffset;
    std::memcpy(staging, slot.indirect.data(), static_cast<size_t>(copyBytes));

    ID3D12Resource* dest = m_gpu->args[frameSlot].Get();
    // This slot is copied once per view, and those records are not back to back.
    if (m_gpu->argState[frameSlot] != D3D12_RESOURCE_STATE_COPY_DEST)
    {
        transitionBuffer(cmd, dest, m_gpu->argState[frameSlot], D3D12_RESOURCE_STATE_COPY_DEST);
        m_gpu->argState[frameSlot] = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    cmd->CopyBufferRegion(dest, dstOffset, m_gpu->argStaging.Get(), srcOffset, copyBytes);
    transitionBuffer(cmd, dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    m_gpu->argState[frameSlot] = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    m_argsCopied[view.index]   = true;
    return true;
}

void DrawList::setupCull(ID3D12Device* device, GpuUploads& gpu)
{
    m_cull.reset();
    gpu.cullUpload = createUpload(device, kCullSlotBytes * kUploadSlots, "CreateCommittedResource (draw list cull upload)");
    if (!gpu.cullUpload || FailedHr(gpu.cullUpload->Map(0, nullptr, reinterpret_cast<void**>(&gpu.cullBytes)), "Map draw list cull upload"))
    {
        gpu.cullBytes = nullptr;
        gpu.cullUpload.Reset();
        DE_LOG_ERROR(LogCategory::Render, "DrawList: instance cull unavailable; static draws stay on the CPU list");
        return;
    }
    gpu.cullUpload->SetName(L"DrawListCullUpload");

    std::unique_ptr<InstanceCull> cull = std::make_unique<InstanceCull>();
    if (!cull->create(device))
    {
        gpu.cullUpload->Unmap(0, nullptr);
        gpu.cullBytes = nullptr;
        gpu.cullUpload.Reset();
        DE_LOG_ERROR(LogCategory::Render, "DrawList: instance cull unavailable; static draws stay on the CPU list");
        return;
    }
    m_cull = std::move(cull);
}

void DrawList::setGpuCull(bool enabled)
{
    m_gpuCull = enabled && m_indirect && cullReady();
}

bool DrawList::cullReady() const
{
    return m_cull && m_cull->isReady() && m_gpu && m_gpu->cullBytes != nullptr;
}

bool DrawList::dispatchCull(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_gpuCull || !cullReady() || !m_gpu || !m_gpu->cullUpload)
        return false;

    compile();
    CullViewDesc views[kMaxStaticViews];
    uint32_t viewCount = 0;
    const uint32_t itemCount = static_cast<uint32_t>(m_items.size());
    for (uint32_t i = 0; i < kMaxStaticViews; ++i)
    {
        const ViewSlot& slot = m_views[i];
        if (!slot.built || !slot.gpu)
            continue;
        const uint32_t batches = (slot.pass == RenderPassKind::Depth) ? static_cast<uint32_t>(m_depth.size()) : static_cast<uint32_t>(m_gbuffer.size());
        if (batches == 0 || batches > kMaxBatches)
            continue;
        CullViewDesc& desc  = views[viewCount++];
        desc.viewIndex      = i;
        desc.batchCount     = batches;
        desc.itemCount      = itemCount;
        desc.useDepthBatch  = slot.pass == RenderPassKind::Depth ? 1u : 0u;
        std::memcpy(desc.planes, slot.planes, sizeof(desc.planes));
    }
    if (viewCount == 0)
        return false;

    const uint32_t frameSlot = m_frameIndex % kUploadSlots;
    const D3D12_GPU_VIRTUAL_ADDRESS base = m_gpu->cullUpload->GetGPUVirtualAddress() + static_cast<UINT64>(frameSlot) * kCullSlotBytes;
    const GpuScope scope(cmd, "Instance Cull");
    const bool ok = m_cull->dispatch(
        cmd,
        frameSlot,
        base,
        base + kCullAabbBytes,
        base + kCullAabbBytes + kCullRecordBytes,
        m_gpu->args[frameSlot].Get(),
        m_gpu->argState[frameSlot],
        views,
        viewCount);
    m_cullDispatched = ok;
    return ok;
}

uint32_t DrawList::exportGpuViews(CullViewDesc* out, uint32_t capacity) const
{
    if (!out || capacity == 0)
        return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxStaticViews && count < capacity; ++i)
    {
        const ViewSlot& slot = m_views[i];
        if (!slot.built || !slot.gpu)
            continue;
        CullViewDesc& desc = out[count++];
        desc               = CullViewDesc{};
        desc.viewIndex     = i;
        desc.useDepthBatch = slot.pass == RenderPassKind::Depth ? 1u : 0u;
        std::memcpy(desc.planes, slot.planes, sizeof(desc.planes));
    }
    return count;
}

ID3D12Resource* DrawList::argumentResource(uint32_t frameSlot) const
{
    if (!m_gpu || frameSlot >= kUploadSlots)
        return nullptr;
    return m_gpu->args[frameSlot].Get();
}

ID3D12Resource* DrawList::cullIndexResource(uint32_t frameSlot) const
{
    if (!m_cull)
        return nullptr;
    return m_cull->indexResource(frameSlot);
}

} // namespace Dark
