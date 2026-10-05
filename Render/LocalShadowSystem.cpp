#include "Render/LocalShadowSystem.h"

#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Sphere3f.h"
#include "Render/Camera3D.h"
#include "Render/DepthState.h"
#include "Render/Profile.h"
#include "Render/ShadowPipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Dark
{

using namespace Math;

namespace
{

constexpr int   kMaxShadowCandidates = 256;
constexpr float kLocalShadowMaxRange = 80.0f; // gatherLocalLights default maxRange
constexpr float kMinShadowRange      = 0.05f;

bool FailedHr(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr))
        return false;
    DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

bool finite3(const Vector3f& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finiteQuat(const Quaternion& q)
{
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
}

bool finiteMatrix(const Matrix4f& m)
{
    for (float v : m.m_afEntry)
    {
        if (!std::isfinite(v))
            return false;
    }
    return true;
}

Vector3f finiteDir(const Vector3f& dir)
{
    if (!finite3(dir) || dir.MagnitudeSqrd() <= 1.0e-8f)
        return Vector3f(Vector3f::Z_AXIS);
    Vector3f n = dir;
    n.Normalize();
    return n;
}

float clampedOuterRad(float outerDeg)
{
    float outer = DegreesToRadians(outerDeg);
    const float kMaxOuter = HalfPi - 0.01f;
    if (outer > kMaxOuter)
        outer = kMaxOuter;
    if (outer < 0.0f)
        outer = 0.0f;
    return outer;
}

struct Candidate
{
    Entity   entity{};
    EntityID id       = 0;
    float    score    = 0.0f;
    float    range    = 0.0f;
    float    tanHalf  = 1.0f;
    float    outerDeg = 0.0f;
    bool     point    = false;
    Vector3f pos{ Vector3f::ZERO };
    Vector3f dir{ Vector3f::Z_AXIS };
};

void warnOverCap(int eligible, int kept)
{
    using clock = std::chrono::steady_clock;
    static clock::time_point s_last{};
    static bool              s_armed = false;
    const auto               now     = clock::now();
    if (s_armed && now - s_last < std::chrono::seconds(1))
        return;
    s_armed = true;
    s_last  = now;
    DE_LOG_WARN(LogCategory::Render, "local shadow lights {} eligible, {} kept, {} skipped", eligible, kept, eligible - kept);
}

bool waitQueue(ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE event)
{
    if (FailedHr(queue->Signal(fence, 1), "Signal (local shadow dummy)"))
        return false;
    if (fence->GetCompletedValue() < 1)
    {
        if (FailedHr(fence->SetEventOnCompletion(1, event), "SetEventOnCompletion (local shadow dummy)"))
            return false;
        WaitForSingleObject(event, INFINITE);
    }
    return true;
}

} // namespace

bool LocalShadowSystem::create(ID3D12Device* device, const LocalShadowSettings& settings)
{
    m_createAttempted = true;
    m_createOk        = false;
    m_settings        = settings;
    if (m_settings.mapSize == 0)
        m_settings.mapSize = 1024;
    if (m_settings.slicesPerFrame == 0 || m_settings.slicesPerFrame > kLocalShadowSlicesPerFrame)
        m_settings.slicesPerFrame = kLocalShadowSlicesPerFrame;
    if (m_settings.maxShadowedLights == 0 || m_settings.maxShadowedLights > kMaxShadowedLocalLights)
        m_settings.maxShadowedLights = kMaxShadowedLocalLights;
    if (!(m_settings.nearPlane > 0.0f))
        m_settings.nearPlane = 0.05f;

    if (!device)
    {
        DE_LOG_ERROR(LogCategory::Render, "LocalShadowSystem: null device");
        return false;
    }

    // Dummy first so an array failure still publishes a fail-open texel.
    createDummy(device);
    const bool recordsOk = createRecords(device);
    const bool arrayOk   = createArray(device);
    m_createOk           = arrayOk && recordsOk;
    if (!m_createOk)
        return false;

    const uint64_t bytes = static_cast<uint64_t>(m_settings.mapSize) * m_settings.mapSize * 4ull
        * kLocalShadowSlicesPerFrame * kLocalShadowFrames;
    DE_LOG_INFO(
        LogCategory::Render,
        "LocalShadowSystem: {}px, {} slices/frame, {} frames, {} bytes",
        m_settings.mapSize,
        m_settings.slicesPerFrame,
        kLocalShadowFrames,
        bytes);
    return true;
}

bool LocalShadowSystem::createDummy(ID3D12Device* device)
{
    m_dummy.Reset();
    m_dummyHeap.Reset();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = 1;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (FailedHr(
            device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_dummy)),
            "CreateCommittedResource local shadow dummy"))
        return false;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT                               rows     = 0;
    UINT64                             rowSize  = 0;
    UINT64                             uploadBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &uploadBytes);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = uploadBytes;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.SampleDesc       = { 1, 0 };
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    if (FailedHr(
            device->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
            "CreateCommittedResource local shadow dummy upload"))
    {
        m_dummy.Reset();
        return false;
    }

    void* mapped = nullptr;
    if (FailedHr(upload->Map(0, nullptr, &mapped), "Map local shadow dummy upload"))
    {
        m_dummy.Reset();
        return false;
    }
    std::memset(mapped, 0, static_cast<size_t>(uploadBytes));
    upload->Unmap(0, nullptr);

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence>               fence;
    if (FailedHr(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue (local shadow dummy)")
        || FailedHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator (local shadow dummy)")
        || FailedHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList (local shadow dummy)")
        || FailedHr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence (local shadow dummy)"))
    {
        m_dummy.Reset();
        return false;
    }

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event)
    {
        DE_LOG_ERROR(LogCategory::Render, "CreateEventW (local shadow dummy) failed");
        m_dummy.Reset();
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource        = m_dummy.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource       = upload.Get();
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = m_dummy.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);

    bool ok = !FailedHr(list->Close(), "Close (local shadow dummy)");
    if (ok)
    {
        ID3D12CommandList* lists[] = { list.Get() };
        queue->ExecuteCommandLists(1, lists);
        ok = waitQueue(queue.Get(), fence.Get(), event);
    }
    CloseHandle(event);
    if (!ok)
    {
        m_dummy.Reset();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dummyHeap)), "CreateDescriptorHeap local shadow dummy SRV"))
    {
        m_dummy.Reset();
        return false;
    }

    m_cpu.arraySrv = m_dummyHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                  = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(m_dummy.Get(), &srv, m_cpu.arraySrv);
    return true;
}

bool LocalShadowSystem::createRecords(ID3D12Device* device)
{
    m_records.Reset();
    m_recordsMap = nullptr;
    m_cpu.recordsSrv = {};

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = sizeof(GpuLocalShadowRecord) * kLocalShadowRecordCount;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FailedHr(
            device->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_records)),
            "CreateCommittedResource local shadow records"))
        return false;

    if (FailedHr(m_records->Map(0, nullptr, reinterpret_cast<void**>(&m_recordsMap)), "Map local shadow records"))
    {
        m_records.Reset();
        return false;
    }
    std::memset(m_recordsMap, 0, sizeof(GpuLocalShadowRecord) * kLocalShadowRecordCount);

    // Array SRV occupies slot 0 when the real map exists. Records stay in slot 1
    // so an array failure can still publish the record view.
    if (!m_srvHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap local shadow SRV"))
        {
            m_records.Reset();
            m_recordsMap = nullptr;
            return false;
        }
    }

    const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_cpu.recordsSrv = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cpu.recordsSrv.ptr += incr;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format                  = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement     = 0;
    srv.Buffer.NumElements      = kLocalShadowRecordCount;
    srv.Buffer.StructureByteStride = sizeof(GpuLocalShadowRecord);
    srv.Buffer.Flags            = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(m_records.Get(), &srv, m_cpu.recordsSrv);
    return true;
}

bool LocalShadowSystem::createArray(ID3D12Device* device)
{
    m_resource.Reset();
    m_dsvHeap.Reset();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = m_settings.mapSize;
    desc.Height           = m_settings.mapSize;
    desc.DepthOrArraySize = static_cast<UINT16>(kLocalShadowSlicesPerFrame * kLocalShadowFrames);
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format             = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = kDepthClear;

    if (FailedHr(
            device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_resource)),
            "CreateCommittedResource local shadow map"))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = kLocalShadowSlicesPerFrame * kLocalShadowFrames;
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FailedHr(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap local shadow DSV"))
    {
        m_resource.Reset();
        return false;
    }

    m_dsvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int frame = 0; frame < static_cast<int>(kLocalShadowFrames); ++frame)
    {
        for (int i = 0; i < static_cast<int>(kLocalShadowSlicesPerFrame); ++i)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format                          = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension                   = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.MipSlice         = 0;
            dsvDesc.Texture2DArray.FirstArraySlice  = static_cast<UINT>(frame * kLocalShadowSlicesPerFrame + i);
            dsvDesc.Texture2DArray.ArraySize        = 1;
            device->CreateDepthStencilView(m_resource.Get(), &dsvDesc, dsv);
            m_dsvCpu[frame][i] = dsv;
            dsv.ptr += m_dsvIncr;
        }
        m_state[frame] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    if (!m_srvHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap local shadow SRV"))
        {
            m_resource.Reset();
            return false;
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE arraySrv = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                         = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Shader4ComponentMapping        = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2DArray.MipLevels       = 1;
    srv.Texture2DArray.ArraySize       = kLocalShadowSlicesPerFrame * kLocalShadowFrames;
    srv.Texture2DArray.FirstArraySlice = 0;
    device->CreateShaderResourceView(m_resource.Get(), &srv, arraySrv);
    m_cpu.arraySrv = arraySrv;
    return true;
}

void LocalShadowSystem::clearSelection()
{
    m_faceCount = 0;
    m_slotCount = 0;
}

void LocalShadowSystem::writeRecords(World& world)
{
    GpuLocalShadowRecord half[kMaxShadowedLocalLights]{};
    for (int s = 0; s < m_slotCount; ++s)
    {
        const Entity entity{ m_slots[s].id };
        const LocalLightComponent* light = world.get<LocalLightComponent>(entity);
        if (!light)
            continue;

        const bool  spot      = light->type == LocalLightType::Spot && light->outerConeDeg > 0.0f;
        const float range     = Min(light->range, kLocalShadowMaxRange);
        const float tanHalf   = spot ? std::tan(clampedOuterRad(light->outerConeDeg)) : 1.0f;
        const int   faceCount = spot ? 1 : kLocalShadowPointFaces;

        GpuLocalShadowRecord& rec = half[m_slots[s].ordinal];
        rec.faceCount = static_cast<float>(faceCount);
        rec.mapSize   = static_cast<float>(m_settings.mapSize);
        rec.depthBias = m_settings.depthBias;
        rec.strength  = 1.0f;

        for (int f = 0; f < m_faceCount; ++f)
        {
            if (m_faces[f].lightOrdinal != m_slots[s].ordinal)
                continue;
            const int faceInLight = m_faces[f].faceInLight;
            if (faceInLight < 0 || faceInLight >= kLocalShadowPointFaces)
                continue;
            GpuLocalShadowFace& gpu = rec.faces[faceInLight];
            std::memcpy(gpu.viewProj, m_faces[f].viewProj.m_afEntry, sizeof(gpu.viewProj));
            gpu.zn          = m_settings.nearPlane;
            gpu.zf          = range;
            gpu.tanHalfFov  = tanHalf;
            gpu.slice       = static_cast<float>(sliceIndex(f));
        }
    }

    if (!m_recordsMap)
        return;
    auto* dst = reinterpret_cast<GpuLocalShadowRecord*>(m_recordsMap);
    std::memcpy(dst + static_cast<size_t>(m_frame) * kMaxShadowedLocalLights, half, sizeof(half));
}

void LocalShadowSystem::update(World& world, const Camera3D& camera, const Frustum3f& cameraFrustum, uint32_t frameIndex, bool select)
{
    m_frameIndex = frameIndex;
    m_frame      = static_cast<int>(frameIndex % kLocalShadowFrames);
    m_haveUpdate = true;

    const bool blocked = !select || !m_debugEnabled || (m_createAttempted && !m_createOk);
    if (blocked)
    {
        clearSelection();
        if (m_recordsMap)
        {
            auto* dst = reinterpret_cast<GpuLocalShadowRecord*>(m_recordsMap);
            std::memset(dst + static_cast<size_t>(m_frame) * kMaxShadowedLocalLights, 0, sizeof(GpuLocalShadowRecord) * kMaxShadowedLocalLights);
        }
        return;
    }

    const Vector3f camPos = camera.GetPosition();
    Candidate      cands[kMaxShadowCandidates];
    int            stored   = 0;
    int            eligible = 0;
    bool           rejected = false;

    world.each<LocalLightComponent>([&](Entity e, LocalLightComponent& light) {
        if (!light.enabled || !light.castShadow || !(light.intensity > 0.0f) || !(light.range > kMinShadowRange))
            return;
        const TransformComponent* xf = world.get<TransformComponent>(e);
        if (!xf || !finite3(xf->position) || !finiteQuat(xf->rotation))
        {
            rejected = true;
            return;
        }

        const float range = Min(light.range, kLocalShadowMaxRange);
        if (!(range > m_settings.nearPlane))
        {
            rejected = true;
            return;
        }
        if (!cameraFrustum.Intersects(Sphere3f(xf->position, range)))
            return;

        ++eligible;
        if (stored >= kMaxShadowCandidates)
            return;

        const bool spot = light.type == LocalLightType::Spot && light.outerConeDeg > 0.0f;
        Candidate  c{};
        c.entity = e;
        c.id     = e.id();
        c.range  = range;
        c.point  = !spot;
        c.pos    = xf->position;
        c.dir    = spot ? finiteDir(xf->rotation.Rotate(Vector3f::Z_AXIS)) : Vector3f(Vector3f::Z_AXIS);
        c.outerDeg = spot ? light.outerConeDeg : 0.0f;
        c.tanHalf  = spot ? std::tan(clampedOuterRad(light.outerConeDeg)) : 1.0f;
        const Vector3f toCam = c.pos - camPos;
        const float    dist  = toCam.Magnitude();
        c.score = light.intensity / (dist * dist + 1.0f);
        for (int h = 0; h < m_heldCount; ++h)
        {
            if (m_held[h] == c.id)
            {
                c.score *= 1.10f;
                break;
            }
        }
        cands[stored++] = c;
    });

    if (rejected && !m_loggedReject)
    {
        m_loggedReject = true;
        DE_LOG_WARN(LogCategory::Render, "Local shadow skipped a non-finite light or a range inside the near plane");
    }

    std::sort(cands, cands + stored, [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.id < b.id;
    });

    clearSelection();
    const int maxLights = static_cast<int>(m_settings.maxShadowedLights);
    const int maxSlices = static_cast<int>(m_settings.slicesPerFrame);
    int       slicesUsed = 0;

    for (int i = 0; i < stored; ++i)
    {
        if (m_slotCount >= maxLights)
            break;
        const Candidate& c    = cands[i];
        const int        need = c.point ? kLocalShadowPointFaces : 1;
        if (slicesUsed + need > maxSlices)
            continue;

        Matrix4f  built[kLocalShadowPointFaces];
        bool      ok    = true;
        const int faces = c.point ? kLocalShadowPointFaces : 1;
        for (int f = 0; f < faces; ++f)
        {
            built[f] = c.point
                ? buildPointFaceViewProj(c.pos, f, c.range, m_settings.nearPlane)
                : buildSpotShadowViewProj(c.pos, c.dir, c.range, c.outerDeg, m_settings.nearPlane);
            if (!finiteMatrix(built[f]))
                ok = false;
        }
        if (!ok)
        {
            rejected = true;
            continue;
        }

        const int ordinal = m_slotCount;
        m_slots[m_slotCount].id      = c.id;
        m_slots[m_slotCount].ordinal = ordinal;
        ++m_slotCount;

        for (int f = 0; f < faces; ++f)
        {
            LocalShadowFaceDraw& draw = m_faces[m_faceCount];
            draw.viewProj     = built[f];
            draw.frustum      = Frustum3f(built[f]);
            draw.lightOrdinal = ordinal;
            draw.faceInLight  = f;
            draw.firstFace    = (f == 0);
            draw.light        = c.entity;
            ++m_faceCount;
        }
        slicesUsed += need;
    }

    if (rejected && !m_loggedReject)
    {
        m_loggedReject = true;
        DE_LOG_WARN(LogCategory::Render, "Local shadow skipped a non-finite light or a range inside the near plane");
    }

    const int skipped = eligible - m_slotCount;
    if (skipped > 0)
        warnOverCap(eligible, m_slotCount);

    EntityID chosen[kMaxShadowedLocalLights]{};
    EntityID logged[kMaxShadowedLocalLights]{};
    for (int i = 0; i < m_slotCount; ++i)
        chosen[i] = m_slots[i].id;
    for (int i = 0; i < m_loggedCount; ++i)
        logged[i] = m_logged[i];
    std::sort(chosen, chosen + m_slotCount);
    std::sort(logged, logged + m_loggedCount);
    bool same = m_slotCount == m_loggedCount;
    for (int i = 0; same && i < m_slotCount; ++i)
        same = chosen[i] == logged[i];
    if (!same)
    {
        char line[256];
        int  used = std::snprintf(line, sizeof(line), "local shadows selected %d", m_slotCount);
        for (int s = 0; s < m_slotCount && used > 0 && used < static_cast<int>(sizeof(line)); ++s)
        {
            int sliceBegin = -1;
            int sliceEnd   = -1;
            bool point     = false;
            for (int f = 0; f < m_faceCount; ++f)
            {
                if (m_faces[f].lightOrdinal != m_slots[s].ordinal)
                    continue;
                if (sliceBegin < 0)
                    sliceBegin = f;
                sliceEnd = f + 1;
                point    = m_faces[f].faceInLight > 0 || (sliceEnd - sliceBegin) > 1;
            }
            used += std::snprintf(
                line + used,
                sizeof(line) - static_cast<size_t>(used),
                " %u %s [%d,%d)",
                static_cast<unsigned>(m_slots[s].id),
                point ? "point" : "spot",
                sliceBegin,
                sliceEnd);
        }
        DE_LOG_INFO(LogCategory::Render, "{}", line);
        m_loggedCount = m_slotCount;
        for (int i = 0; i < m_slotCount; ++i)
            m_logged[i] = m_slots[i].id;
    }

    m_heldCount = m_slotCount;
    for (int i = 0; i < m_slotCount; ++i)
        m_held[i] = m_slots[i].id;

    writeRecords(world);
}

int LocalShadowSystem::recordFor(Entity e) const
{
    if (!m_haveUpdate)
        return -1;
    const EntityID id = e.id();
    for (int i = 0; i < m_slotCount; ++i)
    {
        if (m_slots[i].id == id)
            return m_frame * static_cast<int>(kMaxShadowedLocalLights) + m_slots[i].ordinal;
    }
    return -1;
}

const LocalShadowFaceDraw& LocalShadowSystem::face(int i) const
{
    if (i < 0 || i >= m_faceCount)
    {
        DE_ASSERT(false);
        return m_faces[0];
    }
    return m_faces[static_cast<size_t>(i)];
}

float LocalShadowSystem::debugSlice(int faceOrdinal) const
{
    return static_cast<float>(m_frame * slicesPerFrame() + faceOrdinal);
}

UINT LocalShadowSystem::sliceIndex(int faceInFrame) const
{
    return static_cast<UINT>(m_frame * slicesPerFrame() + faceInFrame);
}

void LocalShadowSystem::closeProfileFace(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_profileFace)
        return;
    profileGpuEnd(cmd);
    profileCpuEnd();
    m_profileFace = false;
}

void LocalShadowSystem::beginCapture(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_resource)
        return;
    if (!m_profileCapture)
    {
        profileCpuBegin("Local Shadows", ProfileColor::Shadows);
        profileGpuBegin(cmd, "Local Shadows", ProfileColor::Shadows);
        m_profileCapture = true;
    }
    if (m_state[m_frame] != D3D12_RESOURCE_STATE_DEPTH_WRITE)
    {
        D3D12_RESOURCE_BARRIER barriers[kLocalShadowSlicesPerFrame]{};
        const int slices = slicesPerFrame();
        for (int i = 0; i < slices; ++i)
        {
            barriers[i].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource   = m_resource.Get();
            barriers[i].Transition.StateBefore = m_state[m_frame];
            barriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            barriers[i].Transition.Subresource = sliceIndex(i);
        }
        cmd->ResourceBarrier(static_cast<UINT>(slices), barriers);
        m_state[m_frame] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // Unused slices stay at clear 0 so an inverted debug tile is white.
    const int slices = slicesPerFrame();
    for (int i = 0; i < slices; ++i)
        cmd->ClearDepthStencilView(m_dsvCpu[m_frame][i], D3D12_CLEAR_FLAG_DEPTH, kDepthClear, 0, 0, nullptr);
}

void LocalShadowSystem::beginFace(ID3D12GraphicsCommandList* cmd, int faceOrdinal)
{
    if (!cmd || faceOrdinal < 0 || faceOrdinal >= m_faceCount || !m_resource)
        return;

    closeProfileFace(cmd);
    char name[40];
    std::snprintf(name, sizeof(name), "Local Shadow Face %d", faceOrdinal);
    profileCpuBegin(name, ProfileColor::ShadowCascade);
    profileGpuBegin(cmd, name, ProfileColor::ShadowCascade);
    m_profileFace = true;

    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(m_settings.mapSize);
    vp.Height   = static_cast<float>(m_settings.mapSize);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(m_settings.mapSize), static_cast<LONG>(m_settings.mapSize) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_dsvCpu[m_frame][faceOrdinal]);
    cmd->ClearDepthStencilView(m_dsvCpu[m_frame][faceOrdinal], D3D12_CLEAR_FLAG_DEPTH, kDepthClear, 0, 0, nullptr);
    if (m_depthPipeline)
    {
        m_depthPipeline->bind(cmd);
        m_depthPipeline->setWvp(cmd, m_faces[faceOrdinal].viewProj.m_afEntry);
    }
}

void LocalShadowSystem::endCapture(ID3D12GraphicsCommandList* cmd)
{
    if (cmd)
        closeProfileFace(cmd);
    if (!cmd || !m_resource)
    {
        if (cmd && m_profileCapture)
        {
            profileGpuEnd(cmd);
            profileCpuEnd();
            m_profileCapture = false;
        }
        return;
    }
    if (m_state[m_frame] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    {
        D3D12_RESOURCE_BARRIER barriers[kLocalShadowSlicesPerFrame]{};
        const int slices = slicesPerFrame();
        for (int i = 0; i < slices; ++i)
        {
            barriers[i].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource   = m_resource.Get();
            barriers[i].Transition.StateBefore = m_state[m_frame];
            barriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[i].Transition.Subresource = sliceIndex(i);
        }
        cmd->ResourceBarrier(static_cast<UINT>(slices), barriers);
        m_state[m_frame] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (m_profileCapture)
    {
        profileGpuEnd(cmd);
        profileCpuEnd();
        m_profileCapture = false;
    }
}

} // namespace Dark
