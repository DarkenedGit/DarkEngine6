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

    // One system, called from Sandbox and Editor. Pools stay out of the world.
    class GpuParticleSystem
    {
    public:
        static constexpr uint32_t kFrameCount             = 2;
        static constexpr uint32_t kMaxParticlesPerEmitter = 262144;
        static constexpr uint32_t kMaxParticleSlotsTotal  = 524288;
        static constexpr uint32_t kThreads                = 64;
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
        // Sets the CPU fallback bit before ParticleEmitter::update. prepare runs after that tick.
        void     syncCpuFallback(World& world);
        void     prepare(World& world, float dt);
        uint32_t poolCount() const;
        bool     simulateAndDraw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, World& world, AssetManager& assets, bool lateDepthWrite);

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
            Math::Vector3f         position{};
            Math::Quaternion       rotation = Math::Quaternion::IDENTITY;
            ComPtr<ID3D12Resource> particles[kFrameCount];
            ComPtr<ID3D12Resource> cb;
            uint8_t*               mapped = nullptr;
            D3D12_RESOURCE_STATES  state[kFrameCount] = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
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
        bool  createBuffers(ID3D12Device* device, Pool& pool, uint32_t capacity, uint32_t generation);
        void        releasePool(Pool& pool);
        void        recordClock(Pool& pool, class ParticleEmitter& emitter, float dt);
        bool        ensureDefaultSprite(Renderer& renderer, AssetManager& assets);

        bool                   m_ready         = false;
        Renderer*              m_renderer      = nullptr;
        uint32_t               m_prepareEpoch  = 0;
        uint32_t               m_consumedEpoch = 0;
        uint32_t               m_drawnFrame    = ~0u;
        bool                   m_loggedSprite  = false;
        ParticleGpuPipeline    m_pipeline;
        std::vector<Pool>      m_pools;
        std::vector<EntityID>  m_records;
        std::vector<EntityID>  m_loggedRefusal;
        std::vector<EntityID>  m_loggedPrewarm;
        std::vector<EntityID>  m_createFailed;
        AssetRef<Material>     m_billboardMat;
    };

} // namespace Dark
