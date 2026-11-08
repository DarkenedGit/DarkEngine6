#include "Particles/GpuParticleSystem.h"

#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "ECS/World.h"
#include "Math/Matrix4f.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleMaterials.h"
#include "Particles/ParticleSpawn.h"
#include "Particles/ParticleTypes.h"
#include "Render/Camera3D.h"
#include "Render/GpuResourceCache.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/Texture2D.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

namespace Dark
{

    namespace
    {

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        bool (*g_testHook)(void* user, int op, uint64_t bytes, ID3D12Resource** out) = nullptr;
        void* g_testHookUser = nullptr;

        ComPtr<ID3D12Resource> hookedResource(UINT64 bytes)
        {
            ID3D12Resource* raw = nullptr;
            if (!g_testHook(g_testHookUser, kGpuParticleTestCommit, bytes, &raw) || !raw)
            {
                if (raw)
                    raw->Release();
                return nullptr;
            }
            ComPtr<ID3D12Resource> res;
            res.Attach(raw);
            return res;
        }

        static_assert(sizeof(GpuParticleConstants) == 512, "gpu particle constants");
        static_assert(offsetof(GpuParticleConstants, sim0) == 224, "gpu particle sim0");
        static_assert(offsetof(GpuParticleConstants, sim2) == 256, "gpu particle sim2");
        static_assert(offsetof(GpuParticleConstants, invViewProj) == 272, "gpu particle invViewProj");
        static_assert(offsetof(GpuParticleConstants, child1) == 496, "gpu particle child1");
        static_assert(sizeof(GpuParticleConstants) <= GpuParticleSystem::kCbStride, "gpu particle cb stride");
        static_assert(GpuParticleSystem::kMaxSecondaryPerFrameHard == kMaxSecondaryPerFrameHard, "secondary event cap");

        constexpr uint32_t kCounterBytes = 256;
        constexpr uint32_t kViewBase     = 2;
        constexpr uint32_t kViewsPerPool = 3;

        uint32_t packRgba8(const float c[4])
        {
            // particleBirthSample: R in bits 24..31, A in bits 0..7.
            auto u8 = [](float v) -> uint32_t {
                if (!std::isfinite(v) || v < 0.0f)
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                return static_cast<uint32_t>(v * 255.0f + 0.5f);
            };
            return (u8(c[0]) << 24) | (u8(c[1]) << 16) | (u8(c[2]) << 8) | u8(c[3]);
        }

        bool wantsSecondary(const ParticleEmitterDesc& desc)
        {
            return desc.collision == ParticleCollisionReaction::SpawnSecondary && desc.hasSecondary && desc.secondary.maxParticles > 0;
        }

        bool secondaryMismatch(bool haveResources, uint32_t childCapacity, const ParticleEmitterDesc& desc)
        {
            const bool want = wantsSecondary(desc);
            if (want != haveResources)
                return true;
            return want && childCapacity != desc.secondary.maxParticles;
        }

        ComPtr<ID3D12Resource> createDefaultBuffer(ID3D12Device* device, UINT64 bytes, D3D12_RESOURCE_STATES state, const char* what)
        {
            if (g_testHook)
                return hookedResource(bytes);
            if (!device)
                return nullptr;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        ComPtr<ID3D12Resource> createUploadBuffer(ID3D12Device* device, UINT64 bytes, const char* what)
        {
            if (g_testHook)
                return hookedResource(bytes);
            if (!device)
                return nullptr;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        void transitionResource(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES after)
        {
            if (!cmd || !resource || state == after)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.StateBefore = state;
            barrier.Transition.StateAfter  = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            state = after;
        }

        void uavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource)
        {
            if (!cmd || !resource)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            cmd->ResourceBarrier(1, &barrier);
        }

        bool markOnce(std::vector<EntityID>& ids, EntityID id)
        {
            for (EntityID have : ids)
            {
                if (have == id)
                    return false;
            }
            ids.push_back(id);
            return true;
        }

        bool containsId(const std::vector<EntityID>& ids, EntityID id)
        {
            for (EntityID have : ids)
            {
                if (have == id)
                    return true;
            }
            return false;
        }

        ComPtr<ID3D12Resource> createDummyTexture(ID3D12Device* device, DXGI_FORMAT format, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width            = 1;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = format;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        void createTextureSrv(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                  = format;
            srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels     = 1;
            device->CreateShaderResourceView(resource, &srv, dest);
        }

        void createCounterUav(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE dest)
        {
            // One uint. The 256-byte allocation keeps the UAV on its own resource from the event buffer.
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format              = DXGI_FORMAT_R32_UINT;
            uav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements  = 1;
            device->CreateUnorderedAccessView(resource, nullptr, &uav, dest);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(ID3D12DescriptorHeap* heap, uint32_t index, UINT increment)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) * increment;
            return handle;
        }

        uint32_t counterDescIndex(int descSlot, uint32_t frameSlot)
        {
            return kViewBase + static_cast<uint32_t>(descSlot) * kViewsPerPool + frameSlot;
        }

        uint32_t headDescIndex(int descSlot)
        {
            return kViewBase + static_cast<uint32_t>(descSlot) * kViewsPerPool + 2u;
        }

        uint64_t particlePoolBytes(uint32_t capacity)
        {
            return static_cast<uint64_t>(capacity) * sizeof(GpuParticle) * GpuParticleSystem::kFrameCount
                + static_cast<uint64_t>(GpuParticleSystem::kCbStride) * GpuParticleSystem::kFrameCount;
        }

    } // namespace

    static_assert(GpuParticleSystem::kFrameCount == Renderer::kFrameCount, "particle slots follow the renderer frame count");
    static_assert(GpuParticleSystem::kMaxParticlesPerEmitter == kMaxGpuParticlesPerEmitter, "gpu particle cap");

    void setGpuParticleTestHook(bool (*fn)(void* user, int op, uint64_t bytes, ID3D12Resource** out), void* user)
    {
        g_testHook     = fn;
        g_testHookUser = user;
    }

    float GpuParticleSystem::clampedDt(float dt, float simulationSpeed)
    {
        if (!std::isfinite(dt) || !std::isfinite(simulationSpeed))
            return 0.0f;
        const float scaled = dt * simulationSpeed;
        if (!std::isfinite(scaled) || scaled <= 0.0f)
            return 0.0f;
        if (scaled > kMaxDt)
            return kMaxDt;
        return scaled;
    }

    uint32_t GpuParticleSystem::substepCount(float dt, float simulationSpeed)
    {
        const float scaled = clampedDt(dt, simulationSpeed);
        if (scaled <= 0.0f)
            return 0u;
        if (scaled <= kSubstepDt)
            return 1u;
        return 2u;
    }

    bool GpuParticleSystem::create(Renderer& renderer)
    {
        if (m_ready && m_pipeline.isValid() && m_viewHeap)
            return true;

        ID3D12Device* device = renderer.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: pipeline create failed; GPU emitters stay undrawn");
            m_ready = false;
            return false;
        }
        if (!m_pipeline.create(device, renderer.sceneColorFormat()) || !createGpuViews(device))
        {
            destroyGpuViews();
            m_pipeline.destroy();
            DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: pipeline create failed; GPU emitters stay undrawn");
            m_ready = false;
            return false;
        }
        m_renderer = &renderer;
        m_ready    = true;
        return true;
    }

    void GpuParticleSystem::destroy(Renderer& renderer)
    {
        renderer.waitForGpu();
        for (Pool& pool : m_pools)
            releasePool(pool);
        m_pools.clear();
        m_records.clear();
        m_loggedSlotCap.clear();
        m_loggedFrozen.clear();
        m_loggedFlagOff.clear();
        m_loggedCreateFailed.clear();
        m_loggedPrewarm.clear();
        m_loggedOrtho.clear();
        m_createFailed.clear();
        m_billboardMat = {};
        destroyGpuViews();
        m_pipeline.destroy();
        m_ready               = false;
        m_renderer            = nullptr;
        m_gpuParticlesEnabled = true;
        m_prepareEpoch        = 0;
        m_consumedEpoch       = 0;
        m_drawnFrame          = ~0u;
        m_loggedSprite        = false;
    }

    GpuParticleSystem::Pool* GpuParticleSystem::findPool(EntityID id)
    {
        for (Pool& pool : m_pools)
        {
            if (pool.entity == id)
                return &pool;
        }
        return nullptr;
    }

    void GpuParticleSystem::releasePool(Pool& pool)
    {
        if (pool.cb && pool.mapped)
        {
            pool.cb->Unmap(0, nullptr);
            pool.mapped = nullptr;
        }
        pool.cb.Reset();
        for (uint32_t slot = 0; slot < kFrameCount; ++slot)
        {
            pool.particles[slot].Reset();
            pool.events[slot].Reset();
            pool.counters[slot].Reset();
            pool.child[slot].Reset();
            pool.state[slot]        = D3D12_RESOURCE_STATE_COMMON;
            pool.eventState[slot]   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            pool.counterState[slot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            pool.childState[slot]   = D3D12_RESOURCE_STATE_COMMON;
        }
        pool.childHead.Reset();
        pool.childHeadState   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pool.childHeadCleared = false;
        pool.childCapacity    = 0;
        if (pool.descSlot >= 0)
            freeDescSlot(pool.descSlot);
        pool.descSlot   = -1;
        pool.capacity   = 0;
        pool.submitted  = false;
        pool.hasSpawned = false;
        pool.mapped     = nullptr;
        pool.lastSlot   = -1;
    }

    bool GpuParticleSystem::hasLiveBuffers(const Pool& pool) const
    {
        return pool.capacity > 0 && pool.mapped != nullptr && pool.particles[0] != nullptr && pool.particles[1] != nullptr && pool.cb;
    }

    bool GpuParticleSystem::createGpuViews(ID3D12Device* device)
    {
        destroyGpuViews();
        if (!device)
            return false;

        const UINT count = kViewBase + kMaxSecondaryPools * kViewsPerPool;
        D3D12_DESCRIPTOR_HEAP_DESC gpuDesc{};
        gpuDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        gpuDesc.NumDescriptors = count;
        gpuDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&gpuDesc, IID_PPV_ARGS(&m_viewHeap)), "CreateDescriptorHeap gpu particles"))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC cpuDesc = gpuDesc;
        cpuDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FailedHr(device->CreateDescriptorHeap(&cpuDesc, IID_PPV_ARGS(&m_viewCpuHeap)), "CreateDescriptorHeap gpu particle cpu"))
        {
            destroyGpuViews();
            return false;
        }
        m_viewInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        m_dummyDepth  = createDummyTexture(device, DXGI_FORMAT_R32_FLOAT, "CreateCommittedResource particle dummy depth");
        m_dummyAttrib = createDummyTexture(device, DXGI_FORMAT_R8G8B8A8_UNORM, "CreateCommittedResource particle dummy attrib");
        m_dummyUav    = createDefaultBuffer(device, kCounterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource particle dummy uav");
        if (!m_dummyDepth || !m_dummyAttrib || !m_dummyUav)
        {
            destroyGpuViews();
            return false;
        }
        m_dummyDepth->SetName(L"DE.Particle.DummyDepth");
        m_dummyAttrib->SetName(L"DE.Particle.DummyAttrib");
        m_dummyUav->SetName(L"DE.Particle.DummyUav");

        // CPU heap slots are the copy sources. Shader-visible slots are seeded so a missed copy is still a valid SRV.
        createTextureSrv(device, m_dummyDepth.Get(), DXGI_FORMAT_R32_FLOAT, cpuAt(m_viewCpuHeap.Get(), 0, m_viewInc));
        createTextureSrv(device, m_dummyAttrib.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpuAt(m_viewCpuHeap.Get(), 1, m_viewInc));
        createTextureSrv(device, m_dummyDepth.Get(), DXGI_FORMAT_R32_FLOAT, cpuAt(m_viewHeap.Get(), 0, m_viewInc));
        createTextureSrv(device, m_dummyAttrib.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpuAt(m_viewHeap.Get(), 1, m_viewInc));
        return true;
    }

    void GpuParticleSystem::destroyGpuViews()
    {
        m_dummyDepth.Reset();
        m_dummyAttrib.Reset();
        m_dummyUav.Reset();
        m_viewHeap.Reset();
        m_viewCpuHeap.Reset();
        m_viewInc = 0;
        for (uint32_t i = 0; i < kMaxSecondaryPools; ++i)
            m_descUsed[i] = false;
    }

    int GpuParticleSystem::allocDescSlot()
    {
        for (uint32_t i = 0; i < kMaxSecondaryPools; ++i)
        {
            if (m_descUsed[i])
                continue;
            m_descUsed[i] = true;
            return static_cast<int>(i);
        }
        return -1;
    }

    void GpuParticleSystem::freeDescSlot(int slot)
    {
        if (slot < 0 || static_cast<uint32_t>(slot) >= kMaxSecondaryPools)
            return;
        m_descUsed[slot] = false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GpuParticleSystem::viewCpu(uint32_t index) const
    {
        // Non-shader-visible. ClearUnorderedAccessViewUint rejects a shader-visible CPU handle.
        return cpuAt(m_viewCpuHeap.Get(), index, m_viewInc);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GpuParticleSystem::viewGpu(uint32_t index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_viewHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * m_viewInc;
        return handle;
    }

    bool GpuParticleSystem::createBuffers(ID3D12Device* device, Pool& pool, const ParticleEmitterDesc& desc, uint32_t generation)
    {
        const uint32_t capacity = desc.maxParticles;
        if (capacity == 0 || capacity > kMaxParticlesPerEmitter)
            return false;
        if (!device && !g_testHook)
            return false;

        const bool     want     = wantsSecondary(desc);
        const uint32_t childCap = want ? desc.secondary.maxParticles : 0u;
        if (want && (childCap == 0 || childCap > kMaxSecondaryParticles || !m_viewHeap || !m_viewCpuHeap))
            return false;

        int newSlot = -1;
        if (want)
        {
            newSlot = allocDescSlot();
            if (newSlot < 0)
                return false;
        }

        const UINT64 particleBytes = static_cast<UINT64>(capacity) * sizeof(GpuParticle);
        ComPtr<ID3D12Resource> particles[kFrameCount];
        ComPtr<ID3D12Resource> events[kFrameCount];
        ComPtr<ID3D12Resource> counters[kFrameCount];
        ComPtr<ID3D12Resource> child[kFrameCount];
        ComPtr<ID3D12Resource> childHead;
        for (uint32_t slot = 0; slot < kFrameCount; ++slot)
        {
            particles[slot] = createDefaultBuffer(device, particleBytes, D3D12_RESOURCE_STATE_COMMON, "CreateCommittedResource gpu particles");
            if (!particles[slot])
            {
                freeDescSlot(newSlot);
                return false;
            }
        }

        if (want)
        {
            const UINT64 eventBytes = static_cast<UINT64>(kMaxSecondaryPerFrameHard) * sizeof(GpuSpawnEvent);
            const UINT64 childBytes = static_cast<UINT64>(childCap) * sizeof(GpuParticle);
            for (uint32_t slot = 0; slot < kFrameCount; ++slot)
            {
                events[slot]   = createDefaultBuffer(device, eventBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource gpu particle events");
                counters[slot] = createDefaultBuffer(device, kCounterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource gpu particle counter");
                child[slot]    = createDefaultBuffer(device, childBytes, D3D12_RESOURCE_STATE_COMMON, "CreateCommittedResource gpu particle child");
                if (!events[slot] || !counters[slot] || !child[slot])
                {
                    freeDescSlot(newSlot);
                    return false;
                }
            }
            childHead = createDefaultBuffer(device, kCounterBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "CreateCommittedResource gpu particle child head");
            if (!childHead)
            {
                freeDescSlot(newSlot);
                return false;
            }

            for (uint32_t slot = 0; slot < kFrameCount; ++slot)
            {
                const uint32_t index = counterDescIndex(newSlot, slot);
                createCounterUav(device, counters[slot].Get(), cpuAt(m_viewHeap.Get(), index, m_viewInc));
                createCounterUav(device, counters[slot].Get(), viewCpu(index));
            }
            const uint32_t headIndex = headDescIndex(newSlot);
            createCounterUav(device, childHead.Get(), cpuAt(m_viewHeap.Get(), headIndex, m_viewInc));
            createCounterUav(device, childHead.Get(), viewCpu(headIndex));
        }

        ComPtr<ID3D12Resource> cb = createUploadBuffer(device, static_cast<UINT64>(kCbStride) * kFrameCount, "CreateCommittedResource gpu particle cb");
        if (!cb)
        {
            freeDescSlot(newSlot);
            return false;
        }

        uint8_t* mapped = nullptr;
        if (FailedHr(cb->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map gpu particle cb"))
        {
            freeDescSlot(newSlot);
            return false;
        }
        std::memset(mapped, 0, static_cast<size_t>(kCbStride) * kFrameCount);

        if (pool.cb && pool.mapped)
        {
            pool.cb->Unmap(0, nullptr);
            pool.mapped = nullptr;
        }
        if (pool.descSlot >= 0 && pool.descSlot != newSlot)
            freeDescSlot(pool.descSlot);

        for (uint32_t slot = 0; slot < kFrameCount; ++slot)
        {
            pool.particles[slot]    = std::move(particles[slot]);
            pool.events[slot]       = std::move(events[slot]);
            pool.counters[slot]     = std::move(counters[slot]);
            pool.child[slot]        = std::move(child[slot]);
            pool.state[slot]        = D3D12_RESOURCE_STATE_COMMON;
            pool.eventState[slot]   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            pool.counterState[slot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            pool.childState[slot]   = D3D12_RESOURCE_STATE_COMMON;
        }
        pool.childHead            = std::move(childHead);
        pool.childHeadState       = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pool.childHeadCleared     = false;
        pool.childCapacity        = childCap;
        pool.descSlot             = newSlot;
        pool.cb                   = std::move(cb);
        pool.mapped               = mapped;
        pool.capacity             = capacity;
        pool.generation           = generation;
        // One dead fill. The next dispatch reads this slot through the ping-pong buffers.
        pool.resetFramesRemaining = 1;
        pool.lastSlot             = -1;
        pool.submitted            = false;
        pool.hasSpawned           = false;
        if (pool.head >= capacity)
            pool.head = 0;
        return true;
    }

    void GpuParticleSystem::recordClock(Pool& pool, ParticleEmitter& emitter, float dt)
    {
        const ParticleEmitCommand command = emitter.emitCommand();
        if (pool.sawClearSerial && command.clearSerial != pool.clearSerial)
        {
            pool.clearSerial          = command.clearSerial;
            pool.resetFramesRemaining = 1;
            if (command.prewarm && markOnce(m_loggedPrewarm, pool.entity))
                DE_LOG_INFO(LogCategory::Render, "GPU particle prewarm ignored for '{}'", emitter.desc().name);
        }
        else if (!pool.sawClearSerial)
        {
            pool.clearSerial    = command.clearSerial;
            pool.sawClearSerial = true;
        }

        const ParticleEmitterDesc& desc = emitter.desc();
        const float                used = clampedDt(dt, desc.simulationSpeed);
        float                      age  = command.age;
        bool                       playing = command.playing;
        if (playing && used > 0.0f)
        {
            age += used;
            if (desc.duration > 0.0f && age >= desc.duration)
            {
                if (desc.looping)
                    age = 0.0f;
                else
                    playing = false;
            }
        }

        const uint32_t burst = emitter.takeBurst();
        const float    rate  = (playing && desc.emissionRate > 0.0f) ? desc.emissionRate : 0.0f;
        const ParticleSpawnStep step = particleAdvanceSpawn(command.emitCarry, rate, used, pool.capacity, pool.head, burst);
        pool.head       = step.head;
        pool.spawnCount = step.spawnCount;
        pool.simDt      = used;
        pool.position   = emitter.position();
        pool.rotation   = emitter.rotation();
        if (step.spawnCount > 0)
            pool.hasSpawned = true;
        emitter.noteGpuClock(step.carry, age, playing);
    }

    bool GpuParticleSystem::ensureDefaultSprite(Renderer& renderer, AssetManager& assets)
    {
        if (m_billboardMat && m_billboardMat->isValid())
            return true;
        m_billboardMat = internParticleSpriteMaterial(assets, false);
        if (!m_billboardMat || !m_billboardMat->isValid())
        {
            if (!m_loggedSprite)
            {
                m_loggedSprite = true;
                DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: default sprite failed");
            }
            return false;
        }
        renderer.gpuResources().ensureMaterial(m_billboardMat);
        return true;
    }

    void GpuParticleSystem::applyFallback(World& world, std::vector<FallbackRow>& accepted, std::vector<EntityID>& seen)
    {
        accepted.clear();
        seen.clear();
        uint32_t live = 0;

        world.each<ParticleEmitterComponent>([&](Entity e, ParticleEmitterComponent& pe) {
            ensureParticleRuntime(pe);
            if (!pe.runtime)
                return;
            const EntityID id = e.id();
            seen.push_back(id);
            ParticleEmitter&           emitter = *pe.runtime;
            const ParticleEmitterDesc& desc    = emitter.desc();
            if (desc.simMode != ParticleSimMode::Gpu)
            {
                std::erase(m_createFailed, id);
                return;
            }

            const uint32_t cap    = desc.maxParticles;
            const uint32_t slots  = cap + (wantsSecondary(desc) ? desc.secondary.maxParticles : 0u);
            const bool     slotOk = static_cast<uint64_t>(live) + static_cast<uint64_t>(slots) <= static_cast<uint64_t>(kMaxParticleSlotsTotal);
            const ParticlePoolDecision decision = particlePoolDecision(desc.simMode, cap, m_gpuParticlesEnabled, slotOk);
            const bool createFailed = containsId(m_createFailed, id);
            const bool keepCpu      = createFailed && cap <= kMaxCpuParticles;
            const bool cpuAlready   = emitter.forceCpu();
            if (!decision.recordGpuPool)
            {
                const uint64_t bytes = particlePoolBytes(cap);
                if (!slotOk && markOnce(m_loggedSlotCap, id))
                {
                    DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: slot cap {} refused '{}' ({} live slots, {} requested, {} bytes)",
                        kMaxParticleSlotsTotal, desc.name, live, cap, static_cast<unsigned long long>(bytes));
                }
                if (!decision.forceCpu)
                {
                    if (markOnce(m_loggedFrozen, id))
                    {
                        DE_LOG_WARN(LogCategory::Render, "GpuParticleSystem: GPU emitter '{}' above {} frozen ({} bytes)",
                            desc.name, kMaxCpuParticles, static_cast<unsigned long long>(bytes));
                    }
                }
                else if (!m_gpuParticlesEnabled && markOnce(m_loggedFlagOff, id))
                {
                    DE_LOG_INFO(LogCategory::Render, "GpuParticleSystem: refused pool '{}' capacity {} bytes {}",
                        desc.name, cap, static_cast<unsigned long long>(bytes));
                }
                emitter.setForceCpu(decision.forceCpu);
                return;
            }

            live += slots;
            emitter.setForceCpu(keepCpu);
            FallbackRow row{};
            row.id         = id;
            row.capacity   = cap;
            row.generation = emitter.gpuDescGeneration();
            row.cpuAlready = cpuAlready;
            accepted.push_back(row);
        });
    }

    void GpuParticleSystem::applyIntegratorFallback(World& world, bool gpuParticlesEnabled)
    {
        m_gpuParticlesEnabled = gpuParticlesEnabled;
        std::vector<FallbackRow> accepted;
        std::vector<EntityID>    seen;
        applyFallback(world, accepted, seen);
    }

    void GpuParticleSystem::prepare(World& world, float dt)
    {
        ++m_prepareEpoch;
        m_records.clear();

        std::vector<FallbackRow> accepted;
        std::vector<EntityID>    seen;
        applyFallback(world, accepted, seen);
        auto dropUnseen = [&seen](std::vector<EntityID>& ids) {
            std::erase_if(ids, [&seen](EntityID id) { return !containsId(seen, id); });
        };
        dropUnseen(m_loggedSlotCap);
        dropUnseen(m_loggedFrozen);
        dropUnseen(m_loggedFlagOff);
        dropUnseen(m_loggedCreateFailed);
        dropUnseen(m_loggedPrewarm);
        dropUnseen(m_loggedOrtho);
        dropUnseen(m_createFailed);

        if (!g_testHook && (!m_ready || !m_renderer || !m_renderer->device()))
            return;

        auto isAccepted = [&accepted](EntityID id) {
            for (const FallbackRow& row : accepted)
            {
                if (row.id == id)
                    return true;
            }
            return false;
        };

        bool needWait = false;
        for (const Pool& pool : m_pools)
        {
            if (!pool.submitted)
                continue;
            const bool drop = !isAccepted(pool.entity);
            bool       recreate = false;
            if (!drop)
            {
                for (const FallbackRow& row : accepted)
                {
                    if (row.id != pool.entity)
                        continue;
                    bool secBad = false;
                    if (ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ pool.entity }))
                    {
                        if (pe->runtime)
                        {
                            const bool have = pool.descSlot >= 0 && pool.events[0] && pool.child[0] && pool.counters[0] && pool.childHead;
                            secBad          = secondaryMismatch(have, pool.childCapacity, pe->runtime->desc());
                        }
                    }
                    recreate = pool.generation != row.generation || pool.capacity != row.capacity || !hasLiveBuffers(pool) || secBad;
                    break;
                }
            }
            if (drop || recreate)
                needWait = true;
        }
        if (needWait && m_renderer)
            m_renderer->waitForGpu();

        for (size_t i = 0; i < m_pools.size();)
        {
            if (!isAccepted(m_pools[i].entity))
            {
                releasePool(m_pools[i]);
                m_pools.erase(m_pools.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }

        auto dropPool = [this](EntityID id) {
            for (size_t i = 0; i < m_pools.size(); ++i)
            {
                if (m_pools[i].entity != id)
                    continue;
                releasePool(m_pools[i]);
                m_pools.erase(m_pools.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        };

        ID3D12Device* device = m_renderer ? m_renderer->device() : nullptr;
        for (const FallbackRow& row : accepted)
        {
            ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ row.id });
            if (!pe || !pe->runtime)
                continue;
            ParticleEmitter& emitter = *pe->runtime;

            Pool*      pool    = findPool(row.id);
            const bool haveSec = pool && pool->descSlot >= 0 && pool->events[0] && pool->child[0] && pool->counters[0] && pool->childHead;
            const bool missing = !pool || pool->generation != row.generation || pool->capacity != row.capacity || !hasLiveBuffers(*pool)
                                 || secondaryMismatch(haveSec, pool ? pool->childCapacity : 0u, emitter.desc());
            if (!pool)
            {
                Pool fresh{};
                fresh.entity = row.id;
                fresh.seed   = row.id == 0 ? 1u : row.id;
                m_pools.push_back(std::move(fresh));
                pool = findPool(row.id);
            }
            if (missing)
            {
                if (!pool || !createBuffers(device, *pool, emitter.desc(), row.generation))
                {
                    // Release once. A submitted pool already waited this frame; later frames have nothing to wait on.
                    if (markOnce(m_loggedCreateFailed, row.id))
                    {
                        DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: pool create failed for '{}' capacity {} bytes {}",
                            emitter.desc().name, row.capacity, static_cast<unsigned long long>(particlePoolBytes(row.capacity)));
                    }
                    if (row.capacity <= kMaxCpuParticles)
                    {
                        emitter.setForceCpu(true);
                        markOnce(m_createFailed, row.id);
                    }
                    else
                        std::erase(m_createFailed, row.id);
                    dropPool(row.id);
                    continue;
                }
                uint64_t bytes = particlePoolBytes(row.capacity);
                if (wantsSecondary(emitter.desc()))
                {
                    const uint64_t childCap = emitter.desc().secondary.maxParticles;
                    bytes += static_cast<uint64_t>(kMaxSecondaryPerFrameHard) * sizeof(GpuSpawnEvent) * kFrameCount;
                    bytes += static_cast<uint64_t>(kCounterBytes) * kFrameCount;
                    bytes += kCounterBytes;
                    bytes += childCap * sizeof(GpuParticle) * kFrameCount;
                }
                DE_LOG_INFO(LogCategory::Render, "GpuParticleSystem: pool '{}' capacity {} bytes {} reaction {}", emitter.desc().name, row.capacity, static_cast<unsigned long long>(bytes),
                            static_cast<unsigned>(emitter.desc().collision));
            }

            pool = findPool(row.id);
            if (!pool || !hasLiveBuffers(*pool))
                continue;
            std::erase(m_createFailed, row.id);
            if (pool->seed == 0)
                pool->seed = row.id == 0 ? 1u : row.id;
            // The CPU arm already integrated this frame. The next sync clears the bit before update.
            if (row.cpuAlready || emitter.forceCpu())
                continue;
            recordClock(*pool, emitter, dt);
            m_records.push_back(row.id);
        }
    }

    uint32_t GpuParticleSystem::poolCount() const
    {
        uint32_t count = 0;
        for (const Pool& pool : m_pools)
        {
            if (hasLiveBuffers(pool))
                ++count;
        }
        return count;
    }

    bool GpuParticleSystem::simulateAndDraw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, World& world, AssetManager& assets, bool lateDepthWrite)
    {
        if (!g_testHook)
        {
            if (!cmd || !m_ready || !m_pipeline.isValid())
                return false;
            DE_ASSERT(cmd == renderer.commandList());
        }
        // Skipped prepare still filters pools from debugState().gpuParticles.
        m_gpuParticlesEnabled = renderer.debugState().gpuParticles;

        const uint32_t frame    = renderer.frameIndex();
        const bool     prepared = m_prepareEpoch != m_consumedEpoch;
        if (prepared)
            m_consumedEpoch = m_prepareEpoch;
        else if (m_drawnFrame == frame)
            return true;

        struct Job
        {
            Pool*                      pool      = nullptr;
            ParticleEmitter*           emitter   = nullptr;
            bool                       simulate      = false;
            bool                       drawable      = false;
            bool                       additive      = true;
            bool                       useAttrib     = false;
            bool                       collide       = false;
            bool                       secondary     = false;
            bool                       childAdditive = false;
            float                      base[4]       = { 1.0f, 1.0f, 1.0f, 1.0f };
            float                      emissive  = 0.0f;
            std::shared_ptr<Texture2D> tex;
        };

        if (!g_testHook)
            ensureDefaultSprite(renderer, assets);

        auto noteSprite = [this]() {
            if (m_loggedSprite)
                return;
            m_loggedSprite = true;
            DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: sprite missing; emitter undrawn");
        };

        auto resolve = [&](ParticleEmitterComponent& pe, ParticleEmitter& emitter, Job& job) {
            job.additive = emitter.desc().additiveBlend;
            job.base[0]  = 1.0f;
            job.base[1]  = 1.0f;
            job.base[2]  = 1.0f;
            job.base[3]  = 1.0f;
            job.emissive = 0.0f;
            job.drawable = false;
            job.tex.reset();

            AssetRef<Material> held;
            const Material*    bindMat = nullptr;
            if (pe.matAssetID != NULL_ASSET)
            {
                held = assets.getAs<Material>(pe.matAssetID);
                if (held && held->isValid())
                {
                    renderer.gpuResources().ensureMaterial(held);
                    bindMat = held.get();
                }
            }
            if (!bindMat && m_billboardMat && m_billboardMat->isValid())
            {
                renderer.gpuResources().ensureMaterial(m_billboardMat);
                bindMat = m_billboardMat.get();
            }
            if (!bindMat || !bindMat->albedo() || bindMat->albedo()->id == NULL_ASSET)
            {
                noteSprite();
                return;
            }

            const float* bc = bindMat->baseColor();
            job.base[0]     = bc[0];
            job.base[1]     = bc[1];
            job.base[2]     = bc[2];
            job.base[3]     = bc[3];
            job.emissive    = bindMat->emissive();
            const AssetID imageId = bindMat->albedo()->id;
            job.tex               = renderer.gpuResources().texture(imageId);
            if (!job.tex || !job.tex->valid())
            {
                noteSprite();
                job.tex.reset();
                return;
            }
            job.drawable = true;
        };

        auto fillJob = [&](ParticleEmitterComponent& pe, ParticleEmitter& emitter, Job& job) {
            if (g_testHook)
            {
                job.additive = emitter.desc().additiveBlend;
                job.drawable = true;
                return;
            }
            resolve(pe, emitter, job);
        };

        std::vector<Job> jobs;
        uint32_t totalSlots = 0;
        for (const Pool& pool : m_pools)
            totalSlots += pool.capacity + pool.childCapacity;

        auto gpuPath = [&](const ParticleEmitter& emitter, uint32_t cap) {
            const uint32_t others = totalSlots >= cap ? totalSlots - cap : 0u;
            const bool slotOk = static_cast<uint64_t>(others) + static_cast<uint64_t>(cap) <= static_cast<uint64_t>(kMaxParticleSlotsTotal);
            const ParticlePoolDecision decision = particlePoolDecision(emitter.desc().simMode, emitter.desc().maxParticles, m_gpuParticlesEnabled, slotOk);
            return decision.recordGpuPool && !emitter.forceCpu();
        };

        if (prepared)
        {
            const bool ortho = camera.IsOrthographic();
            for (EntityID id : m_records)
            {
                Pool* pool = findPool(id);
                if (!pool || !hasLiveBuffers(*pool))
                    continue;
                ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ id });
                if (!pe || !pe->runtime)
                    continue;
                ParticleEmitter& emitter = *pe->runtime;
                if (!gpuPath(emitter, pool->capacity + pool->childCapacity))
                    continue;

                const ParticleEmitterDesc& desc = emitter.desc();
                Job job{};
                job.pool     = pool;
                job.emitter  = &emitter;
                job.simulate = true;
                const bool reacts = desc.collision != ParticleCollisionReaction::None;
                // Orthographic skip: reverse-Z reconstruct is perspective. Integrate and draw still run.
                job.collide = reacts && !ortho;
                job.useAttrib = job.collide && desc.collision == ParticleCollisionReaction::Bounce && renderer.scenePath() == ScenePath::HybridDeferred && renderer.hasGBuffer() && !lateDepthWrite;
                job.secondary = wantsSecondary(desc) && pool->descSlot >= 0 && pool->events[0] && pool->events[1] && pool->counters[0] && pool->counters[1] && pool->child[0] && pool->child[1] && pool->childHead;
                job.childAdditive = desc.secondary.additiveBlend;
                if (ortho && reacts && markOnce(m_loggedOrtho, id))
                    DE_LOG_INFO(LogCategory::Render, "GpuParticleSystem: orthographic camera skips collision for '{}'", desc.name);
                fillJob(*pe, emitter, job);
                jobs.push_back(std::move(job));
            }
        }
        else
        {
            for (Pool& pool : m_pools)
            {
                if (pool.lastSlot < 0 || !hasLiveBuffers(pool))
                    continue;
                ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ pool.entity });
                if (!pe || !pe->runtime)
                    continue;
                if (!gpuPath(*pe->runtime, pool.capacity + pool.childCapacity))
                    continue;
                Job job{};
                job.pool          = &pool;
                job.emitter       = pe->runtime.get();
                job.simulate      = false;
                job.secondary     = pool.childCapacity > 0 && pool.child[0] && pool.child[1] && pool.events[0];
                job.childAdditive = pe->runtime->desc().secondary.additiveBlend;
                fillJob(*pe, *pe->runtime, job);
                if (!job.drawable)
                    continue;
                jobs.push_back(std::move(job));
            }
        }

        if (jobs.empty())
        {
            if (prepared)
                m_drawnFrame = frame;
            return true;
        }

        // Depth sample is this frame's R32_FLOAT. The binds stay transitions and OM only.
        if (!g_testHook)
        {
            if (renderer.hasSceneBuffers())
                renderer.bindHdrDepthCompute();
            else
                renderer.bindSwapChainDepthCompute();
        }

        bool anyAttrib = false;
        for (const Job& job : jobs)
        {
            if (job.simulate && job.useAttrib)
                anyAttrib = true;
        }
        if (anyAttrib)
            renderer.transitionAttrib(cmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        const uint32_t writeSlot = frame % kFrameCount;
        const uint32_t otherSlot = (writeSlot + 1u) % kFrameCount;

        auto writeCb = [&](Pool& pool, const ParticleEmitter& emitter, const Job& job, float substepDt) {
            GpuParticleConstants constants{};
            const Math::Matrix4f  vp = camera.GetViewProj();
            std::memcpy(constants.viewProj, vp.m_afEntry, sizeof(constants.viewProj));
            const Math::Vector3f right = camera.GetRight();
            const Math::Vector3f up    = camera.GetUp();
            constants.cameraRight[0]   = right.x;
            constants.cameraRight[1]   = right.y;
            constants.cameraRight[2]   = right.z;
            constants.cameraUp[0]      = up.x;
            constants.cameraUp[1]      = up.y;
            constants.cameraUp[2]      = up.z;
            constants.baseColor[0]     = job.base[0];
            constants.baseColor[1]     = job.base[1];
            constants.baseColor[2]     = job.base[2];
            constants.baseColor[3]     = job.base[3];

            const ParticleEmitterDesc& desc = emitter.desc();
            constants.gravity[0]            = desc.gravity.x;
            constants.gravity[1]            = desc.gravity.y;
            constants.gravity[2]            = desc.gravity.z;
            constants.emitterPos[0]         = pool.position.x;
            constants.emitterPos[1]         = pool.position.y;
            constants.emitterPos[2]         = pool.position.z;
            constants.emitterRot[0]         = pool.rotation.x;
            constants.emitterRot[1]         = pool.rotation.y;
            constants.emitterRot[2]         = pool.rotation.z;
            constants.emitterRot[3]         = pool.rotation.w;
            constants.direction[0]          = desc.direction.x;
            constants.direction[1]          = desc.direction.y;
            constants.direction[2]          = desc.direction.z;
            constants.shapeSize[0]          = desc.shapeSize.x;
            constants.shapeSize[1]          = desc.shapeSize.y;
            constants.shapeSize[2]          = desc.shapeSize.z;
            constants.lifeSpeed[0]          = desc.lifetime.min;
            constants.lifeSpeed[1]          = desc.lifetime.max;
            constants.lifeSpeed[2]          = desc.startSpeed.min;
            constants.lifeSpeed[3]          = desc.startSpeed.max;
            constants.sizes[0] = desc.startSize.min;
            constants.sizes[1] = desc.startSize.max;
            constants.sizes[2] = desc.endSize.min;
            constants.sizes[3] = desc.endSize.max;
            uint32_t shape     = 0;
            if (desc.shape == ParticleEmitterDesc::Shape::Box)
                shape = 1;
            else if (desc.shape == ParticleEmitterDesc::Shape::Sphere)
                shape = 2;
            constants.sim0[0] = pool.capacity;
            constants.sim0[1] = pool.head;
            constants.sim0[2] = pool.seed;
            constants.sim0[3] = shape;
            constants.sim1[0] = packRgba8(desc.startColor);
            constants.sim1[1] = packRgba8(desc.endColor);
            constants.sim2[0] = job.emissive;
            constants.sim2[1] = kParticleEmissiveGain;
            constants.sim2[2] = desc.spreadDegrees * (3.14159265f / 180.0f);
            constants.sim2[3] = substepDt;

            const Math::Matrix4f invVp = vp.Inverse();
            const Math::Matrix4f view  = camera.GetView();
            std::memcpy(constants.invViewProj, invVp.m_afEntry, sizeof(constants.invViewProj));
            std::memcpy(constants.view, view.m_afEntry, sizeof(constants.view));
            const Math::Vector3f eye  = camera.GetPosition();
            const Math::Vector3f look = camera.GetLook();
            constants.cameraPos[0]  = eye.x;
            constants.cameraPos[1]  = eye.y;
            constants.cameraPos[2]  = eye.z;
            constants.cameraLook[0] = look.x;
            constants.cameraLook[1] = look.y;
            constants.cameraLook[2] = look.z;
            constants.collide0[0]   = camera.GetNearZ();
            constants.collide0[1]   = static_cast<float>(renderer.width());
            constants.collide0[2]   = static_cast<float>(renderer.height());
            constants.collide0[3]   = desc.collisionBias;
            constants.collide1[0]   = desc.collisionThickness;
            constants.collide1[1]   = desc.restitution;
            constants.collide1[2]   = desc.friction;
            constants.collide1[3]   = desc.restLifetime;
            uint32_t maxSecondary = 0;
            if (job.secondary)
            {
                maxSecondary = desc.maxSecondaryPerFrame;
                if (maxSecondary > kMaxSecondaryPerFrameHard)
                    maxSecondary = kMaxSecondaryPerFrameHard;
            }
            constants.collide2[0] = static_cast<uint32_t>(desc.collision);
            constants.collide2[1] = job.useAttrib ? 1u : 0u;
            constants.collide2[2] = job.collide ? 1u : 0u;
            constants.collide2[3] = maxSecondary;
            constants.child0[0]   = desc.secondary.lifetime.min;
            constants.child0[1]   = desc.secondary.lifetime.max;
            constants.child0[2]   = desc.secondary.startSize.min;
            constants.child0[3]   = desc.secondary.startSize.max;
            constants.child1[0]   = pool.childCapacity;
            uint32_t restBits      = 0;
            std::memcpy(&restBits, &desc.secondary.restLifetime, sizeof(restBits));
            constants.child1[1]   = restBits;

            uint8_t* dst = pool.mapped + static_cast<size_t>(writeSlot) * kCbStride;
            std::memset(dst, 0, kCbStride);
            std::memcpy(dst, &constants, sizeof(constants));
        };

        auto bindUavs = [&](Pool& pool) {
            ID3D12Resource* events  = (pool.events[writeSlot]) ? pool.events[writeSlot].Get() : m_dummyUav.Get();
            ID3D12Resource* counter = (pool.counters[writeSlot]) ? pool.counters[writeSlot].Get() : m_dummyUav.Get();
            ID3D12Resource* head    = pool.childHead ? pool.childHead.Get() : m_dummyUav.Get();
            cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimEvents, events->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimCounter, counter->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimChildHead, head->GetGPUVirtualAddress());
        };

        auto dispatchIntegrate = [&](Pool& pool, ID3D12Resource* read, D3D12_RESOURCE_STATES& readState, ID3D12Resource* write, D3D12_RESOURCE_STATES& writeState, uint32_t capacity, bool inPlace, uint32_t spawnCount, uint32_t reset, uint32_t reaction, uint32_t useAttrib, uint32_t collide, uint32_t flags) {
            if (g_testHook)
            {
                const uint64_t tag = (reset != 0u ? kGpuParticleTestReset : 0ull) | (inPlace ? kGpuParticleTestInPlace : 0ull);
                g_testHook(g_testHookUser, kGpuParticleTestDispatch, tag, nullptr);
                return;
            }
            if (!read || !write || capacity == 0 || !m_dummyUav)
                return;
            if (!inPlace)
                transitionResource(cmd, read, readState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transitionResource(cmd, write, writeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // In-place reads and writes the current slot, so it must not also bind that buffer as an SRV.
            DE_ASSERT(inPlace || read != write);
            m_pipeline.bindSimulate(cmd, inPlace);
            const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = pool.cb->GetGPUVirtualAddress() + static_cast<UINT64>(writeSlot) * kCbStride;
            cmd->SetComputeRootConstantBufferView(ParticleGpuPipeline::kSimCb, cbAddr);
            if (!inPlace)
                cmd->SetComputeRootShaderResourceView(ParticleGpuPipeline::kSimPrev, read->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimCurr, write->GetGPUVirtualAddress());
            const uint32_t step[8] = { spawnCount, reset, capacity, reaction, useAttrib, collide, flags, 0u };
            cmd->SetComputeRoot32BitConstants(ParticleGpuPipeline::kSimStep, 8, step, 0);
            cmd->SetComputeRootDescriptorTable(ParticleGpuPipeline::kSimTextures, viewGpu(0));
            bindUavs(pool);
            const UINT groups = static_cast<UINT>((capacity + kThreads - 1u) / kThreads);
            cmd->Dispatch(groups, 1, 1);
        };

        auto barrierWrites = [&](Pool& pool, bool secondary, ID3D12Resource* particles) {
            uavBarrier(cmd, particles);
            if (!secondary)
                return;
            uavBarrier(cmd, pool.events[writeSlot].Get());
            uavBarrier(cmd, pool.counters[writeSlot].Get());
        };

        if (prepared && cmd && m_viewHeap && renderer.device())
        {
            D3D12_CPU_DESCRIPTOR_HANDLE depthSrc = renderer.depthSrvCpu();
            if (depthSrc.ptr == 0)
                depthSrc = viewCpu(0);
            renderer.device()->CopyDescriptorsSimple(1, cpuAt(m_viewHeap.Get(), 0, m_viewInc), depthSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            D3D12_CPU_DESCRIPTOR_HANDLE attribSrc = viewCpu(1);
            if (anyAttrib)
            {
                const D3D12_CPU_DESCRIPTOR_HANDLE real = renderer.attribSrvCpu();
                if (real.ptr != 0)
                    attribSrc = real;
            }
            renderer.device()->CopyDescriptorsSimple(1, cpuAt(m_viewHeap.Get(), 1, m_viewInc), attribSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            ID3D12DescriptorHeap* heaps[] = { m_viewHeap.Get() };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        if (prepared)
        {
            const GpuScope simScope(cmd, "GPU Particle Sim", ProfileColor::Particles);
            for (Job& job : jobs)
            {
                if (!job.simulate || !job.pool || !job.emitter)
                    continue;
                Pool& pool = *job.pool;
                const GpuParticleBufferStep bufferStep = gpuParticleBufferStep(pool.resetFramesRemaining, pool.lastSlot, writeSlot);
                const bool                  reset      = bufferStep.reset;
                const bool                  inPlace    = bufferStep.inPlace;

                // simDt is already clamped. Speed 1 keeps substepCount from scaling it again.
                uint32_t steps = substepCount(pool.simDt, 1.0f);
                float    h     = 0.0f;
                if (steps == 0)
                    steps = 1;
                else if (steps == 2)
                    h = pool.simDt * 0.5f;
                else
                    h = pool.simDt;
                // One upload. The GPU reads this slot while later dispatches are recorded.
                writeCb(pool, *job.emitter, job, h);

                const uint32_t reaction = static_cast<uint32_t>(job.emitter->desc().collision);
                const uint32_t collide  = job.collide ? 1u : 0u;
                const uint32_t attrib   = job.useAttrib ? 1u : 0u;
                if (cmd && job.secondary)
                {
                    transitionResource(cmd, pool.events[writeSlot].Get(), pool.eventState[writeSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    transitionResource(cmd, pool.counters[writeSlot].Get(), pool.counterState[writeSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    // This slot only. The other counter, childHead, and the event buffer stay.
                    const UINT zeros[4] = { 0, 0, 0, 0 };
                    const uint32_t index = counterDescIndex(pool.descSlot, writeSlot);
                    cmd->ClearUnorderedAccessViewUint(viewGpu(index), viewCpu(index), pool.counters[writeSlot].Get(), zeros, 0, nullptr);
                    uavBarrier(cmd, pool.counters[writeSlot].Get());
                }

                dispatchIntegrate(pool, pool.particles[inPlace ? writeSlot : otherSlot].Get(), pool.state[inPlace ? writeSlot : otherSlot], pool.particles[writeSlot].Get(), pool.state[writeSlot], pool.capacity, inPlace, pool.spawnCount, reset ? 1u : 0u, reaction, attrib, collide, 0u);
                if (steps == 2)
                {
                    if (cmd)
                        barrierWrites(pool, job.secondary, pool.particles[writeSlot].Get());
                    // Same spawn count rewrites the birth pose and skips Euler on the second substep.
                    dispatchIntegrate(pool, pool.particles[writeSlot].Get(), pool.state[writeSlot], pool.particles[writeSlot].Get(), pool.state[writeSlot], pool.capacity, true, pool.spawnCount, 0u, reaction, attrib, collide, 0u);
                }
                if (cmd)
                    barrierWrites(pool, job.secondary, pool.particles[writeSlot].Get());

                if (cmd && job.secondary)
                {
                    // Same reset as the parent so that dispatch does not read an uninitialized child slot.
                    // Stop, no birth, child rest. Runs before consume so a newborn is not aged this frame.
                    ID3D12Resource* childWrite = pool.child[writeSlot].Get();
                    ID3D12Resource* childRead  = pool.child[inPlace ? writeSlot : otherSlot].Get();
                    dispatchIntegrate(pool, childRead, pool.childState[inPlace ? writeSlot : otherSlot], childWrite, pool.childState[writeSlot], pool.childCapacity, inPlace, 0u, reset ? 1u : 0u, static_cast<uint32_t>(ParticleCollisionReaction::Stop), 0u, collide, 1u);
                    if (steps == 2)
                    {
                        barrierWrites(pool, true, childWrite);
                        dispatchIntegrate(pool, childWrite, pool.childState[writeSlot], childWrite, pool.childState[writeSlot], pool.childCapacity, true, 0u, 0u, static_cast<uint32_t>(ParticleCollisionReaction::Stop), 0u, collide, 1u);
                    }
                    barrierWrites(pool, true, childWrite);

                    transitionResource(cmd, pool.childHead.Get(), pool.childHeadState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    if (!pool.childHeadCleared)
                    {
                        // Create leaves the head undefined. A per-frame clear would rewind the ring.
                        const UINT zeros[4] = { 0, 0, 0, 0 };
                        const uint32_t index = headDescIndex(pool.descSlot);
                        cmd->ClearUnorderedAccessViewUint(viewGpu(index), viewCpu(index), pool.childHead.Get(), zeros, 0, nullptr);
                        pool.childHeadCleared = true;
                    }
                    uavBarrier(cmd, pool.childHead.Get());

                    m_pipeline.bindConsume(cmd);
                    transitionResource(cmd, childWrite, pool.childState[writeSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = pool.cb->GetGPUVirtualAddress() + static_cast<UINT64>(writeSlot) * kCbStride;
                    cmd->SetComputeRootConstantBufferView(ParticleGpuPipeline::kSimCb, cbAddr);
                    cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimCurr, childWrite->GetGPUVirtualAddress());
                    const uint32_t step[8] = { 0u, 0u, kMaxSecondaryPerFrameHard, static_cast<uint32_t>(ParticleCollisionReaction::SpawnSecondary), 0u, collide, 1u, 0u };
                    cmd->SetComputeRoot32BitConstants(ParticleGpuPipeline::kSimStep, 8, step, 0);
                    cmd->SetComputeRootDescriptorTable(ParticleGpuPipeline::kSimTextures, viewGpu(0));
                    bindUavs(pool);
                    const UINT consumeGroups = (kMaxSecondaryPerFrameHard + kThreads - 1u) / kThreads;
                    cmd->Dispatch(consumeGroups, 1, 1);
                    uavBarrier(cmd, childWrite);
                    transitionResource(cmd, childWrite, pool.childState[writeSlot], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }

                if (cmd)
                    transitionResource(cmd, pool.particles[writeSlot].Get(), pool.state[writeSlot], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if (pool.resetFramesRemaining > 0)
                    --pool.resetFramesRemaining;
                pool.lastSlot  = static_cast<int>(writeSlot);
                pool.submitted = true;
            }
        }

        {
            const GpuScope drawScope(cmd, "GPU Particle Draw", ProfileColor::Particles);
            auto drawBuffer = [&](Job& job, ID3D12Resource* particles, D3D12_RESOURCE_STATES& state, uint32_t capacity, bool additive) {
                if (!particles || capacity == 0)
                    return;
                if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                    uavBarrier(cmd, particles);
                transitionResource(cmd, particles, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                m_pipeline.bindDraw(cmd, additive);
                const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = job.pool->cb->GetGPUVirtualAddress() + static_cast<UINT64>(writeSlot) * kCbStride;
                cmd->SetGraphicsRootConstantBufferView(ParticleGpuPipeline::kDrawCb, cbAddr);
                cmd->SetGraphicsRootShaderResourceView(ParticleGpuPipeline::kDrawParticles, particles->GetGPUVirtualAddress());
                job.tex->bind(cmd, ParticleGpuPipeline::kDrawSprite);
                // Root constant, not sim0.x, so a child draw cannot walk off the child buffer.
                const uint32_t args[4] = { capacity, 0u, 0u, 0u };
                cmd->SetGraphicsRoot32BitConstants(ParticleGpuPipeline::kDrawArgs, 4, args, 0);
                cmd->DrawInstanced(6, capacity, 0, 0);
            };

            for (Job& job : jobs)
            {
                if (!job.drawable || !job.pool || !job.emitter)
                    continue;
                if (!job.tex && !g_testHook)
                    continue;
                Pool& pool = *job.pool;
                if (!pool.hasSpawned)
                    continue;
                if (g_testHook)
                {
                    g_testHook(g_testHookUser, kGpuParticleTestDraw, 0, nullptr);
                    continue;
                }
                if (pool.lastSlot < 0 || !pool.particles[pool.lastSlot])
                    continue;
                const uint32_t particleSlot = static_cast<uint32_t>(pool.lastSlot);
                if (!job.simulate)
                    writeCb(pool, *job.emitter, job, 0.0f);
                drawBuffer(job, pool.particles[particleSlot].Get(), pool.state[particleSlot], pool.capacity, job.additive);
                if (job.secondary && pool.child[particleSlot] && pool.childCapacity > 0)
                    drawBuffer(job, pool.child[particleSlot].Get(), pool.childState[particleSlot], pool.childCapacity, job.childAdditive);
            }
        }

        m_drawnFrame = frame;
        return true;
    }

} // namespace Dark
