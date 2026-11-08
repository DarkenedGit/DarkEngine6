#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/Material.h"
#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Render/ParticleGpuPipeline.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class World;
    class AssetManager;
    class Camera3D;

    using Microsoft::WRL::ComPtr;

    // Counts CreateCommittedResource, Dispatch, and DrawInstanced when no device is up.
    struct GpuParticleCreateRecord
    {
        int  committed  = 0;
        int  dispatches = 0;
        int  draws      = 0;
        bool failCreate = false;
    };

    // Scalar layout. HLSL ParticleGpu.hlsli uses the same fields, not float3.
    struct GpuParticle
    {
        float    position[3];
        float    life;
        float    velocity[3];
        float    maxLife;
        float    size0;
        float    size1;
        float    rotation;
        float    rest;
        uint32_t color0;
        uint32_t color1;
        uint32_t flags;
        uint32_t seed;
    };
    static_assert(sizeof(GpuParticle) == 64, "gpu particle");

    // Packed event. The counter is a separate UAV so a clear cannot wipe events.
    struct GpuSpawnEvent
    {
        float    position[3];
        float    velocity[3];
        uint32_t packedNormal;
        uint32_t seed;
    };
    static_assert(sizeof(GpuSpawnEvent) == 32, "spawn event");

    // 512 bytes. ParticleGpu.hlsli GpuParticleCb uses the same order.
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
        uint32_t sim0[4];
        uint32_t sim1[4];
        float    sim2[4];
        float    invViewProj[16];
        float    view[16];
        float    cameraPos[4];
        float    cameraLook[4];
        float    collide0[4]; // nearZ, width, height, bias
        float    collide1[4]; // thickness, restitution, friction, parent rest
        uint32_t collide2[4]; // reaction, useAttrib, collideEnable, maxSecondary
        float    child0[4];   // life min/max, start size min/max
        uint32_t child1[4];   // childCapacity, child rest bits, pad, pad
    };

    // One system, called from Sandbox and Editor. Pools stay out of the world.
    class GpuParticleSystem
    {
    public:
        static constexpr uint32_t kFrameCount             = 2;
        static constexpr uint32_t kMaxParticlesPerEmitter = 262144;
        static constexpr uint32_t kMaxParticleSlotsTotal  = 524288;
        static constexpr uint32_t kThreads                = 64;
        static constexpr uint32_t kMaxSecondaryPerFrameHard = 8192;
        static constexpr uint32_t kMaxSecondaryPools      = 64;
        static constexpr float    kMaxDt                  = 1.0f / 30.0f;
        static constexpr float    kSubstepDt              = 1.0f / 60.0f;
        static constexpr uint32_t kCbStride               = 512;

        GpuParticleSystem()  = default;
        ~GpuParticleSystem() = default;

        GpuParticleSystem(const GpuParticleSystem&)            = delete;
        GpuParticleSystem& operator=(const GpuParticleSystem&) = delete;
        GpuParticleSystem(GpuParticleSystem&&)                 = delete;
        GpuParticleSystem& operator=(GpuParticleSystem&&)      = delete;

        // dt * simulationSpeed, clamped to kMaxDt. Non-finite or non-positive input is 0.
        static float clampedDt(float dt, float simulationSpeed);
        // 0 if clamped dt is 0, 1 if clamped dt <= kSubstepDt, else 2.
        static uint32_t substepCount(float dt, float simulationSpeed);

        bool     create(Renderer& renderer);
        void     destroy(Renderer& renderer);
        // particlePoolDecision before the tick. prepare and simulateAndDraw use this flag.
        void     applyIntegratorFallback(World& world, bool gpuParticlesEnabled);
        void     prepare(World& world, float dt);
        uint32_t poolCount() const;
        bool     simulateAndDraw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, World& world, AssetManager& assets, bool lateDepthWrite);
        void     bindCreateRecordForTest(GpuParticleCreateRecord* record);

    private:
        struct Pool
        {
            EntityID               entity               = 0;
            uint32_t               capacity             = 0;
            uint32_t               generation           = 0;
            uint32_t               head                 = 0;
            uint32_t               spawnCount           = 0;
            uint32_t               clearSerial          = 0;
            uint32_t               resetFramesRemaining = 0;
            uint32_t               seed                 = 1;
            float                  simDt                = 0.0f;
            int                    lastSlot             = -1;
            bool                   submitted            = false;
            bool                   sawClearSerial       = false;
            bool                   hasSpawned           = false;
            bool                   recorded             = false;
            Math::Vector3f         position{};
            Math::Quaternion       rotation = Math::Quaternion::IDENTITY;
            ComPtr<ID3D12Resource> particles[kFrameCount];
            ComPtr<ID3D12Resource> events[kFrameCount];
            ComPtr<ID3D12Resource> counters[kFrameCount];
            ComPtr<ID3D12Resource> child[kFrameCount];
            ComPtr<ID3D12Resource> childHead;
            ComPtr<ID3D12Resource> cb;
            uint8_t*               mapped = nullptr;
            uint32_t               childCapacity = 0;
            int                    descSlot      = -1;
            bool                   childHeadCleared = false;
            std::vector<uint8_t>   recordedBytes;
            D3D12_RESOURCE_STATES  state[kFrameCount] = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
            D3D12_RESOURCE_STATES  eventState[kFrameCount] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
            D3D12_RESOURCE_STATES  counterState[kFrameCount] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
            D3D12_RESOURCE_STATES  childState[kFrameCount] = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
            D3D12_RESOURCE_STATES  childHeadState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        };

        struct FallbackRow
        {
            EntityID id         = 0;
            uint32_t capacity   = 0;
            uint32_t generation = 0;
            bool     cpuAlready = false;
        };

        Pool* findPool(EntityID id);
        void  applyFallback(World& world, std::vector<FallbackRow>& accepted, std::vector<EntityID>& seen);
        bool  createBuffers(ID3D12Device* device, Pool& pool, const struct ParticleEmitterDesc& desc, uint32_t generation);
        bool  hasLiveBuffers(const Pool& pool) const;
        bool  createGpuViews(ID3D12Device* device);
        void  destroyGpuViews();
        int   allocDescSlot();
        void  freeDescSlot(int slot);
        D3D12_CPU_DESCRIPTOR_HANDLE viewCpu(uint32_t index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE viewGpu(uint32_t index) const;
        void        releasePool(Pool& pool);
        void        recordClock(Pool& pool, class ParticleEmitter& emitter, float dt);
        bool        ensureDefaultSprite(Renderer& renderer, AssetManager& assets);

        bool                     m_ready               = false;
        Renderer*                m_renderer            = nullptr;
        uint32_t                 m_prepareEpoch        = 0;
        uint32_t                 m_consumedEpoch       = 0;
        uint32_t                 m_drawnFrame          = ~0u;
        bool                     m_loggedSprite        = false;
        bool                     m_gpuParticlesEnabled = true;
        GpuParticleCreateRecord* m_createRecord        = nullptr;
        ParticleGpuPipeline      m_pipeline;
        std::vector<Pool>        m_pools;
        std::vector<EntityID>    m_records;
        std::vector<EntityID>    m_loggedSlotCap;
        std::vector<EntityID>    m_loggedFrozen;
        std::vector<EntityID>    m_loggedFlagOff;
        std::vector<EntityID>    m_loggedCreateFailed;
        std::vector<EntityID>    m_loggedPrewarm;
        std::vector<EntityID>    m_loggedOrtho;
        std::vector<EntityID>    m_createFailed;
        AssetRef<Material>       m_billboardMat;
        ComPtr<ID3D12DescriptorHeap> m_viewHeap;
        ComPtr<ID3D12DescriptorHeap> m_viewCpuHeap;
        ComPtr<ID3D12Resource>       m_dummyDepth;
        ComPtr<ID3D12Resource>       m_dummyAttrib;
        ComPtr<ID3D12Resource>       m_dummyUav;
        UINT                         m_viewInc = 0;
        bool                         m_descUsed[kMaxSecondaryPools] = {};
    };

} // namespace Dark
