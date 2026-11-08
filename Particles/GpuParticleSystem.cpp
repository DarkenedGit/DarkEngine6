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

        // Row-major, matches ParticleGpu.hlsli. Spawn count and reset are root constants, not this buffer.
        struct GpuParticleConstants
        {
            float    viewProj[16];
            float    cameraRight[4];
            float    cameraUp[4];
            float    baseColor[4];
            float    gravity[4];
            float    emitterPos[4];
            float    emitterRot[4];
            float    direction[4];
            float    shapeSize[4];
            float    lifeSpeed[4];
            float    sizes[4];
            float    startColor[4];
            float    endColor[4];
            uint32_t sim0[4];
            uint32_t sim1[4];
            float    sim2[4];
        };
        static_assert(sizeof(GpuParticleConstants) == 304, "gpu particle constants");
        static_assert(offsetof(GpuParticleConstants, sim0) == 256, "gpu particle sim0");
        static_assert(offsetof(GpuParticleConstants, sim2) == 288, "gpu particle sim2");
        static_assert(sizeof(GpuParticleConstants) <= GpuParticleSystem::kCbStride, "gpu particle cb stride");

        uint32_t packRgba8(const float c[4])
        {
            uint32_t packed = 0;
            for (int i = 0; i < 4; ++i)
            {
                float v = c[i];
                if (!std::isfinite(v) || v < 0.0f)
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                const uint32_t byte = static_cast<uint32_t>(v * 255.0f + 0.5f);
                packed |= byte << (i * 8);
            }
            return packed;
        }

        ComPtr<ID3D12Resource> createDefaultBuffer(ID3D12Device* device, UINT64 bytes, D3D12_RESOURCE_STATES state, const char* what)
        {
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

    } // namespace

    static_assert(GpuParticleSystem::kFrameCount == Renderer::kFrameCount, "particle slots follow the renderer frame count");
    static_assert(GpuParticleSystem::kMaxParticlesPerEmitter == kMaxGpuParticlesPerEmitter, "gpu particle cap");

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
        if (m_ready && m_pipeline.isValid())
            return true;

        ID3D12Device* device = renderer.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: pipeline create failed; GPU emitters stay undrawn");
            m_ready = false;
            return false;
        }
        if (!m_pipeline.create(device, renderer.sceneColorFormat()))
        {
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
        m_loggedRefusal.clear();
        m_loggedPrewarm.clear();
        m_billboardMat = {};
        m_pipeline.destroy();
        m_ready         = false;
        m_renderer      = nullptr;
        m_prepareEpoch  = 0;
        m_consumedEpoch = 0;
        m_drawnFrame    = ~0u;
        m_loggedSprite  = false;
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

    const GpuParticleSystem::Pool* GpuParticleSystem::findPool(EntityID id) const
    {
        for (const Pool& pool : m_pools)
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
            pool.state[slot] = D3D12_RESOURCE_STATE_COMMON;
        }
        pool.capacity  = 0;
        pool.submitted = false;
        pool.lastSlot  = -1;
    }

    bool GpuParticleSystem::createBuffers(ID3D12Device* device, Pool& pool, uint32_t capacity, uint32_t generation)
    {
        if (!device || capacity == 0 || capacity > kMaxParticlesPerEmitter)
            return false;

        const UINT64 particleBytes = static_cast<UINT64>(capacity) * sizeof(GpuParticle);
        ComPtr<ID3D12Resource> particles[kFrameCount];
        for (uint32_t slot = 0; slot < kFrameCount; ++slot)
        {
            particles[slot] = createDefaultBuffer(device, particleBytes, D3D12_RESOURCE_STATE_COMMON, "CreateCommittedResource gpu particles");
            if (!particles[slot])
                return false;
        }

        ComPtr<ID3D12Resource> cb = createUploadBuffer(device, static_cast<UINT64>(kCbStride) * kFrameCount, "CreateCommittedResource gpu particle cb");
        if (!cb)
            return false;

        uint8_t* mapped = nullptr;
        if (FailedHr(cb->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "Map gpu particle cb"))
            return false;
        std::memset(mapped, 0, static_cast<size_t>(kCbStride) * kFrameCount);

        if (pool.cb && pool.mapped)
        {
            pool.cb->Unmap(0, nullptr);
            pool.mapped = nullptr;
        }

        for (uint32_t slot = 0; slot < kFrameCount; ++slot)
        {
            pool.particles[slot] = std::move(particles[slot]);
            pool.state[slot]     = D3D12_RESOURCE_STATE_COMMON;
        }
        pool.cb                   = std::move(cb);
        pool.mapped               = mapped;
        pool.capacity             = capacity;
        pool.generation           = generation;
        pool.resetFramesRemaining = kFrameCount;
        pool.lastSlot             = -1;
        pool.submitted            = false;
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
            pool.resetFramesRemaining = kFrameCount;
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

    void GpuParticleSystem::prepare(World& world, float dt)
    {
        ++m_prepareEpoch;
        m_records.clear();
        if (!m_ready || !m_renderer || !m_renderer->device())
            return;

        struct Accept
        {
            EntityID id         = 0;
            uint32_t capacity   = 0;
            uint32_t generation = 0;
        };

        std::vector<Accept>   accepted;
        std::vector<EntityID> seen;
        uint32_t              live = 0;

        world.each<ParticleEmitterComponent>([&](Entity e, ParticleEmitterComponent& pe) {
            ensureParticleRuntime(pe);
            if (!pe.runtime)
                return;
            const EntityID id = e.id();
            seen.push_back(id);
            ParticleEmitter&           emitter = *pe.runtime;
            const ParticleEmitterDesc& desc    = emitter.desc();
            if (desc.simMode != ParticleSimMode::Gpu)
                return;

            // The CPU arm already advanced this emitter. Do not also step the GPU clock.
            if (emitter.forceCpu())
            {
                (void)particlePoolDecision(desc.simMode, desc.maxParticles, true, false);
                return;
            }

            const uint32_t cap    = desc.maxParticles;
            const bool     slotOk = static_cast<uint64_t>(live) + static_cast<uint64_t>(cap) <= static_cast<uint64_t>(kMaxParticleSlotsTotal);
            const ParticlePoolDecision decision = particlePoolDecision(desc.simMode, cap, true, slotOk);
            if (!decision.recordGpuPool)
            {
                if (!slotOk && markOnce(m_loggedRefusal, id))
                {
                    DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: slot cap {} refused '{}' ({} live slots, {} requested)", kMaxParticleSlotsTotal, desc.name, live, cap);
                }
                if (decision.forceCpu && !emitter.forceCpu())
                    emitter.setForceCpu(true);
                return;
            }

            live += cap;
            Accept row{};
            row.id         = id;
            row.capacity   = cap;
            row.generation = emitter.gpuDescGeneration();
            accepted.push_back(row);
        });

        auto isAccepted = [&accepted](EntityID id) {
            for (const Accept& row : accepted)
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
                for (const Accept& row : accepted)
                {
                    if (row.id != pool.entity)
                        continue;
                    recreate = pool.generation != row.generation || pool.capacity != row.capacity || !pool.particles[0] || !pool.cb;
                    break;
                }
            }
            if (drop || recreate)
                needWait = true;
        }
        if (needWait)
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

        ID3D12Device* device = m_renderer->device();
        for (const Accept& row : accepted)
        {
            ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ row.id });
            if (!pe || !pe->runtime)
                continue;
            ParticleEmitter& emitter = *pe->runtime;

            Pool* pool = findPool(row.id);
            const bool missing = !pool || pool->generation != row.generation || pool->capacity != row.capacity || !pool->particles[0] || !pool->cb;
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
                if (!pool || !createBuffers(device, *pool, row.capacity, row.generation))
                {
                    if (markOnce(m_loggedRefusal, row.id))
                        DE_LOG_ERROR(LogCategory::Render, "GpuParticleSystem: pool create failed for '{}' capacity {}", emitter.desc().name, row.capacity);
                    if (row.capacity <= kMaxCpuParticles && !emitter.forceCpu())
                        emitter.setForceCpu(true);
                    if (pool && pool->capacity == 0)
                    {
                        for (size_t i = 0; i < m_pools.size(); ++i)
                        {
                            if (m_pools[i].entity == row.id)
                            {
                                releasePool(m_pools[i]);
                                m_pools.erase(m_pools.begin() + static_cast<std::ptrdiff_t>(i));
                                break;
                            }
                        }
                    }
                    continue;
                }
                const uint64_t bytes = static_cast<uint64_t>(row.capacity) * sizeof(GpuParticle) * kFrameCount + static_cast<uint64_t>(kCbStride) * kFrameCount;
                DE_LOG_INFO(LogCategory::Render, "GpuParticleSystem: pool '{}' capacity {} bytes {} reaction {}", emitter.desc().name, row.capacity, static_cast<unsigned long long>(bytes),
                            static_cast<unsigned>(emitter.desc().collision));
            }

            pool = findPool(row.id);
            if (!pool || !pool->particles[0] || !pool->cb)
                continue;
            if (pool->seed == 0)
                pool->seed = row.id == 0 ? 1u : row.id;
            recordClock(*pool, emitter, dt);
            m_records.push_back(row.id);
        }

        std::erase_if(m_loggedRefusal, [&seen](EntityID id) { return !containsId(seen, id); });
        std::erase_if(m_loggedPrewarm, [&seen](EntityID id) { return !containsId(seen, id); });
    }

    uint32_t GpuParticleSystem::poolCount() const
    {
        uint32_t count = 0;
        for (const Pool& pool : m_pools)
        {
            if (pool.capacity > 0 && pool.particles[0])
                ++count;
        }
        return count;
    }

    bool GpuParticleSystem::simulateAndDraw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, World& world, AssetManager& assets, bool lateDepthWrite)
    {
        (void)lateDepthWrite;
        if (!cmd || !m_ready || !m_pipeline.isValid())
            return false;
        DE_ASSERT(cmd == renderer.commandList());

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
            bool                       simulate  = false;
            bool                       drawable  = false;
            bool                       additive  = true;
            float                      base[4]   = { 1.0f, 1.0f, 1.0f, 1.0f };
            float                      emissive  = 0.0f;
            std::shared_ptr<Texture2D> tex;
        };

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

        std::vector<Job> jobs;
        if (prepared)
        {
            uint32_t totalSlots = 0;
            for (const Pool& pool : m_pools)
                totalSlots += pool.capacity;

            for (EntityID id : m_records)
            {
                Pool* pool = findPool(id);
                if (!pool || !pool->cb || !pool->mapped || !pool->particles[0] || !pool->particles[1] || pool->capacity == 0)
                    continue;
                ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ id });
                if (!pe || !pe->runtime)
                    continue;
                ParticleEmitter& emitter = *pe->runtime;
                if (emitter.desc().simMode != ParticleSimMode::Gpu)
                    continue;
                const uint32_t cap    = pool->capacity;
                const uint32_t others = totalSlots >= cap ? totalSlots - cap : 0u;
                const bool     slotOk = static_cast<uint64_t>(others) + static_cast<uint64_t>(cap) <= static_cast<uint64_t>(kMaxParticleSlotsTotal);
                const ParticlePoolDecision decision = particlePoolDecision(emitter.desc().simMode, emitter.desc().maxParticles, true, slotOk);
                if (emitter.forceCpu() || !decision.recordGpuPool)
                    continue;

                Job job{};
                job.pool     = pool;
                job.emitter  = &emitter;
                job.simulate = true;
                resolve(*pe, emitter, job);
                jobs.push_back(std::move(job));
            }
        }
        else
        {
            for (Pool& pool : m_pools)
            {
                if (pool.lastSlot < 0 || !pool.cb || !pool.mapped || !pool.particles[pool.lastSlot])
                    continue;
                ParticleEmitterComponent* pe = world.get<ParticleEmitterComponent>(Entity{ pool.entity });
                if (!pe || !pe->runtime)
                    continue;
                if (pe->runtime->desc().simMode != ParticleSimMode::Gpu || pe->runtime->forceCpu())
                    continue;
                Job job{};
                job.pool     = &pool;
                job.emitter  = pe->runtime.get();
                job.simulate = false;
                resolve(*pe, *pe->runtime, job);
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

        // Depth test only. The compute root does not sample the depth buffer.
        if (renderer.hasSceneBuffers())
            renderer.bindHdrDepthCompute();
        else
            renderer.bindSwapChainDepthCompute();

        const uint32_t writeSlot = frame % kFrameCount;
        const uint32_t otherSlot = (writeSlot + 1u) % kFrameCount;

        auto writeCb = [&](Pool& pool, const ParticleEmitter& emitter, const float base[4], float emissive, float substepDt) {
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
            constants.baseColor[0]     = base[0];
            constants.baseColor[1]     = base[1];
            constants.baseColor[2]     = base[2];
            constants.baseColor[3]     = base[3];

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
            constants.sizes[0]              = desc.startSize.min;
            constants.sizes[1]              = desc.startSize.max;
            constants.sizes[2]              = desc.endSize.min;
            constants.sizes[3]              = desc.endSize.max;
            constants.startColor[0]         = desc.startColor[0];
            constants.startColor[1]         = desc.startColor[1];
            constants.startColor[2]         = desc.startColor[2];
            constants.startColor[3]         = desc.startColor[3];
            constants.endColor[0]           = desc.endColor[0];
            constants.endColor[1]           = desc.endColor[1];
            constants.endColor[2]           = desc.endColor[2];
            constants.endColor[3]           = desc.endColor[3];
            uint32_t shape                  = 0;
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
            constants.sim2[0] = emissive;
            constants.sim2[1] = kParticleEmissiveGain;
            constants.sim2[2] = desc.spreadDegrees * (3.14159265f / 180.0f);
            constants.sim2[3] = substepDt;

            uint8_t* dst = pool.mapped + static_cast<size_t>(writeSlot) * kCbStride;
            std::memset(dst, 0, kCbStride);
            std::memcpy(dst, &constants, sizeof(constants));
        };

        auto dispatchStep = [&](Pool& pool, bool inPlace, uint32_t spawnCount, uint32_t reset) {
            const uint32_t readSlot = inPlace ? writeSlot : otherSlot;
            if (!inPlace)
                transitionResource(cmd, pool.particles[readSlot].Get(), pool.state[readSlot], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transitionResource(cmd, pool.particles[writeSlot].Get(), pool.state[writeSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // In-place reads and writes the current slot, so it must not also bind that buffer as an SRV.
            DE_ASSERT(inPlace || pool.particles[readSlot].Get() != pool.particles[writeSlot].Get());
            m_pipeline.bindSimulate(cmd, inPlace);
            const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = pool.cb->GetGPUVirtualAddress() + static_cast<UINT64>(writeSlot) * kCbStride;
            cmd->SetComputeRootConstantBufferView(ParticleGpuPipeline::kSimCb, cbAddr);
            if (!inPlace)
                cmd->SetComputeRootShaderResourceView(ParticleGpuPipeline::kSimPrev, pool.particles[readSlot]->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(ParticleGpuPipeline::kSimCurr, pool.particles[writeSlot]->GetGPUVirtualAddress());
            const uint32_t step[4] = { spawnCount, reset, 0u, 0u };
            cmd->SetComputeRoot32BitConstants(ParticleGpuPipeline::kSimStep, 4, step, 0);
            const UINT groups = static_cast<UINT>((pool.capacity + kThreads - 1u) / kThreads);
            cmd->Dispatch(groups, 1, 1);
        };

        if (prepared)
        {
            const GpuScope simScope(cmd, "GPU Particle Sim", ProfileColor::Particles);
            for (Job& job : jobs)
            {
                if (!job.simulate || !job.pool || !job.emitter)
                    continue;
                Pool& pool = *job.pool;
                // A skipped frame can leave the latest copy in this slot. Reading the other slot would bring stale particles back.
                const bool reset   = pool.resetFramesRemaining > 0 || pool.lastSlot < 0;
                bool       inPlace = true;
                if (!reset && pool.lastSlot == static_cast<int>(otherSlot))
                    inPlace = false;

                uint32_t steps = 1;
                float    h     = pool.simDt;
                if (pool.simDt > kSubstepDt)
                {
                    steps = 2;
                    h     = pool.simDt * 0.5f;
                }
                writeCb(pool, *job.emitter, job.base, job.emissive, h);
                dispatchStep(pool, inPlace, pool.spawnCount, reset ? 1u : 0u);
                if (steps == 2)
                {
                    uavBarrier(cmd, pool.particles[writeSlot].Get());
                    dispatchStep(pool, true, 0u, 0u);
                }
                uavBarrier(cmd, pool.particles[writeSlot].Get());
                transitionResource(cmd, pool.particles[writeSlot].Get(), pool.state[writeSlot], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if (pool.resetFramesRemaining > 0)
                    --pool.resetFramesRemaining;
                pool.lastSlot  = static_cast<int>(writeSlot);
                pool.submitted = true;
            }
        }

        {
            const GpuScope drawScope(cmd, "GPU Particle Draw", ProfileColor::Particles);
            for (Job& job : jobs)
            {
                if (!job.drawable || !job.tex || !job.pool || !job.emitter)
                    continue;
                Pool& pool = *job.pool;
                if (pool.lastSlot < 0 || !pool.particles[pool.lastSlot])
                    continue;
                const uint32_t particleSlot = static_cast<uint32_t>(pool.lastSlot);
                if (!job.simulate)
                    writeCb(pool, *job.emitter, job.base, job.emissive, 0.0f);
                if (pool.state[particleSlot] == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                    uavBarrier(cmd, pool.particles[particleSlot].Get());
                transitionResource(cmd, pool.particles[particleSlot].Get(), pool.state[particleSlot], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                m_pipeline.bindDraw(cmd, job.additive);
                const D3D12_GPU_VIRTUAL_ADDRESS cbAddr = pool.cb->GetGPUVirtualAddress() + static_cast<UINT64>(writeSlot) * kCbStride;
                cmd->SetGraphicsRootConstantBufferView(ParticleGpuPipeline::kDrawCb, cbAddr);
                cmd->SetGraphicsRootShaderResourceView(ParticleGpuPipeline::kDrawParticles, pool.particles[particleSlot]->GetGPUVirtualAddress());
                job.tex->bind(cmd, ParticleGpuPipeline::kDrawSprite);
                cmd->DrawInstanced(6, pool.capacity, 0, 0);
            }
        }

        m_drawnFrame = frame;
        return true;
    }

} // namespace Dark
