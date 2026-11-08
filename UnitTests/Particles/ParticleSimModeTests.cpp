#include <gtest/gtest.h>

#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/GpuParticleSystem.h"
#include "Particles/ParticleSpawn.h"
#include "Particles/ParticleTick.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"

#include <wrl/client.h>

#include <charconv>
#include <system_error>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

using namespace Dark;
using namespace Dark::Math;

namespace
{

int g_ribbonLogs = 0;
int g_overCapLogs = 0;
int g_poolCreates = 0;
int g_committedLogs = 0;
int g_frozenLogs = 0;
int g_refusedLogs = 0;
int g_createFailedLogs = 0;
int g_byteLogs = 0;
char g_bytesNeedle[32] = {};

void captureParticleLogs(LogLevel, LogCategory, const char* message)
{
    if (message == nullptr)
        return;
    if (std::strstr(message, "ribbon forces CPU") != nullptr)
        ++g_ribbonLogs;
    if (std::strstr(message, "no CPU pool") != nullptr)
        ++g_overCapLogs;
}

void capturePoolLogs(LogLevel, LogCategory, const char* message)
{
    if (message == nullptr)
        return;
    if (std::strstr(message, "GpuParticleSystem: pool '") != nullptr)
        ++g_poolCreates;
    if (std::strstr(message, "CreateCommittedResource") != nullptr)
        ++g_committedLogs;
    if (std::strstr(message, "frozen") != nullptr)
        ++g_frozenLogs;
    if (std::strstr(message, "refused pool") != nullptr)
        ++g_refusedLogs;
    if (std::strstr(message, "pool create failed") != nullptr)
        ++g_createFailedLogs;
    if (g_bytesNeedle[0] != '\0' && std::strstr(message, g_bytesNeedle) != nullptr)
        ++g_byteLogs;
}

struct LogCapture
{
    explicit LogCapture(Log::CaptureFn fn)
        : m_fn(fn)
    {
        Log::addCapture(m_fn);
    }
    ~LogCapture()
    {
        Log::removeCapture(m_fn);
    }
    Log::CaptureFn m_fn;
};

ParticleEmitterDesc cpuMotionDesc()
{
    ParticleEmitterDesc desc{};
    desc.maxParticles  = 32;
    desc.emissionRate  = 10.0f;
    desc.lifetime      = { 10.0f, 10.0f };
    desc.startSpeed    = { 3.0f, 3.0f };
    desc.direction     = Vector3f(1.0f, 0.0f, 0.0f);
    desc.spreadDegrees = 0.0f;
    desc.gravity       = Vector3f(0.0f, 0.0f, 0.0f);
    desc.shape         = ParticleEmitterDesc::Shape::Point;
    desc.looping       = true;
    return desc;
}

const Particle* firstSpawned(const ParticleEmitter& emitter)
{
    for (const Particle& p : emitter.particles())
    {
        if (p.maxLife > 0.0f)
            return &p;
    }
    return nullptr;
}

Entity addGpuEmitter(World& world, uint32_t maxParticles, float emissionRate)
{
    Entity e = world.createEntity();
    ParticleEmitterComponent pe{};
    pe.desc.simMode      = ParticleSimMode::Gpu;
    pe.desc.maxParticles = maxParticles;
    pe.desc.emissionRate = emissionRate;
    pe.playing           = true;
    world.emplace<ParticleEmitterComponent>(e, std::move(pe));
    return e;
}

struct ParticleHookCounts
{
    int      committed          = 0;
    int      dispatches         = 0;
    int      resetDispatches    = 0;
    int      preserveDispatches = 0;
    int      draws              = 0;
    uint64_t lastDispatchTag    = 0;
    bool     failCreate         = false;
};

struct StubResource final : public ID3D12Resource
{
    explicit StubResource(uint8_t* mem)
        : m_mem(mem)
    {
    }

    ~StubResource()
    {
        delete[] m_mem;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Resource))
        {
            *ppv = static_cast<ID3D12Resource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --m_refs;
        if (left == 0)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void** ppv) override
    {
        if (ppv)
            *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Map(UINT, const D3D12_RANGE*, void** ppData) override
    {
        if (!ppData)
            return E_POINTER;
        *ppData = m_mem;
        return S_OK;
    }
    void STDMETHODCALLTYPE Unmap(UINT, const D3D12_RANGE*) override
    {
    }
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override
    {
        return {};
    }
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override
    {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT, const D3D12_BOX*, const void*, UINT, UINT) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*, UINT, UINT, UINT, const D3D12_BOX*) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS*) override
    {
        return E_NOTIMPL;
    }

    ULONG    m_refs = 1;
    uint8_t* m_mem  = nullptr;
};

bool particleTestHook(void* user, int op, uint64_t bytes, ID3D12Resource** out)
{
    ParticleHookCounts* counts = static_cast<ParticleHookCounts*>(user);
    if (op == kGpuParticleTestDispatch)
    {
        ++counts->dispatches;
        counts->lastDispatchTag = bytes;
        if ((bytes & kGpuParticleTestReset) != 0)
            ++counts->resetDispatches;
        else
            ++counts->preserveDispatches;
        return true;
    }
    if (op == kGpuParticleTestDraw)
    {
        ++counts->draws;
        return true;
    }
    if (op != kGpuParticleTestCommit || counts->failCreate || !out)
        return false;
    const size_t size = bytes == 0 ? 1u : static_cast<size_t>(bytes);
    uint8_t* mem = new (std::nothrow) uint8_t[size];
    if (!mem)
        return false;
    std::memset(mem, 0, size);
    StubResource* resource = new (std::nothrow) StubResource(mem);
    if (!resource)
    {
        delete[] mem;
        return false;
    }
    *out = resource;
    ++counts->committed;
    return true;
}

struct ParticleHookGuard
{
    explicit ParticleHookGuard(ParticleHookCounts* counts)
    {
        setGpuParticleTestHook(&particleTestHook, counts);
    }
    ~ParticleHookGuard()
    {
        setGpuParticleTestHook(nullptr, nullptr);
    }
    ParticleHookGuard(const ParticleHookGuard&)            = delete;
    ParticleHookGuard& operator=(const ParticleHookGuard&) = delete;
};

} // namespace

TEST(ParticleSimMode, DefaultsAreCpuAndNoCollision)
{
    const ParticleEmitterDesc desc{};
    EXPECT_EQ(desc.simMode, ParticleSimMode::Cpu);
    EXPECT_EQ(desc.collision, ParticleCollisionReaction::None);
    EXPECT_FLOAT_EQ(desc.collisionThickness, 0.35f);
    EXPECT_FLOAT_EQ(desc.collisionBias, 0.04f);
    EXPECT_FLOAT_EQ(desc.restitution, 0.45f);
    EXPECT_FLOAT_EQ(desc.friction, 0.20f);
    EXPECT_FLOAT_EQ(desc.restLifetime, 0.20f);
    EXPECT_EQ(desc.maxSecondaryPerFrame, 512u);
    EXPECT_FALSE(desc.hasSecondary);
}

TEST(ParticleSimMode, CpuSetDescClampsAndIntegratesOneEulerStep)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc = cpuMotionDesc();
    desc.maxParticles = 150000;
    desc.emissionRate = 40.0f;
    emitter.setDesc(desc);
    emitter.setTransform(Vector3f(5.0f, 6.0f, 7.0f));
    EXPECT_EQ(emitter.desc().maxParticles, kMaxCpuParticles);
    EXPECT_EQ(emitter.capacity(), kMaxCpuParticles);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Cpu);

    const float dt = 0.1f;
    emitter.update(dt);
    EXPECT_GT(emitter.aliveCount(), 0u);
    const Particle* spawned = firstSpawned(emitter);
    ASSERT_NE(spawned, nullptr);
    EXPECT_TRUE(spawned->alive);
    EXPECT_NEAR(spawned->life, spawned->maxLife - dt, 1.0e-4f);
    EXPECT_NEAR(spawned->position.x, 5.0f + 3.0f * dt, 1.0e-4f);
    EXPECT_NEAR(spawned->position.y, 6.0f, 1.0e-4f);
    EXPECT_NEAR(spawned->position.z, 7.0f, 1.0e-4f);
}

TEST(ParticleSimMode, CpuKillingStepDoesNotMove)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc = cpuMotionDesc();
    desc.lifetime     = { 0.05f, 0.05f };
    desc.emissionRate = 100.0f;
    desc.startSpeed   = { 4.0f, 4.0f };
    desc.direction    = Vector3f(0.0f, 1.0f, 0.0f);
    emitter.setDesc(desc);
    emitter.setTransform(Vector3f(5.0f, 6.0f, 7.0f));
    emitter.update(0.05f);

    EXPECT_EQ(emitter.aliveCount(), 0u);
    const Particle* spawned = firstSpawned(emitter);
    ASSERT_NE(spawned, nullptr);
    EXPECT_FALSE(spawned->alive);
    EXPECT_NEAR(spawned->position.x, 5.0f, 1.0e-4f);
    EXPECT_NEAR(spawned->position.y, 6.0f, 1.0e-4f);
    EXPECT_NEAR(spawned->position.z, 7.0f, 1.0e-4f);
}

TEST(ParticleSimMode, GpuSetDescAllocatesNoCpuVector)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc = cpuMotionDesc();
    desc.simMode      = ParticleSimMode::Gpu;
    desc.maxParticles = 200000;
    desc.emissionRate = 10000.0f;
    emitter.setDesc(desc);
    EXPECT_EQ(emitter.desc().maxParticles, 200000u);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(emitter.capacity(), 0u);

    emitter.update(0.5f);
    EXPECT_EQ(emitter.capacity(), 0u);
    EXPECT_EQ(emitter.aliveCount(), 0u);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Gpu);
}

TEST(ParticleSimMode, GpuSetDescClampsZeroAndUpperBound)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc{};
    desc.simMode      = ParticleSimMode::Gpu;
    desc.maxParticles = 0;
    emitter.setDesc(desc);
    EXPECT_EQ(emitter.desc().maxParticles, 1u);
    EXPECT_EQ(emitter.capacity(), 0u);

    desc.maxParticles = 300000;
    emitter.setDesc(desc);
    EXPECT_EQ(emitter.desc().maxParticles, kMaxGpuParticlesPerEmitter);
    EXPECT_EQ(emitter.capacity(), 0u);
}

TEST(ParticleSimMode, ForceCpuOnSmallGpuDescKeepsModeAndIntegrates)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc = cpuMotionDesc();
    desc.simMode      = ParticleSimMode::Gpu;
    desc.maxParticles = 512;
    desc.emissionRate = 1000.0f;
    desc.startSpeed   = { 0.0f, 0.0f };
    desc.gravity      = Vector3f(0.0f, -10.0f, 0.0f);
    emitter.setDesc(desc);
    emitter.setTransform(Vector3f(1.0f, 2.0f, 3.0f));

    const float dt = 0.1f;
    emitter.update(dt, true);
    EXPECT_GT(emitter.aliveCount(), 0u);
    EXPECT_EQ(emitter.capacity(), 512u);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Gpu);
    EXPECT_TRUE(emitter.forceCpu());
    const Particle* spawned = firstSpawned(emitter);
    ASSERT_NE(spawned, nullptr);
    EXPECT_NEAR(spawned->velocity.y, -10.0f * dt, 1.0e-4f);
    EXPECT_NEAR(spawned->position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(spawned->position.y, 2.0f + (-10.0f * dt) * dt, 1.0e-4f);
    EXPECT_NEAR(spawned->position.z, 3.0f, 1.0e-4f);

    ParticleEmitter stored;
    stored.setDesc(desc);
    stored.setForceCpu(true);
    stored.setTransform(Vector3f(1.0f, 2.0f, 3.0f));
    stored.update(dt);
    EXPECT_GT(stored.aliveCount(), 0u);
    EXPECT_EQ(stored.desc().simMode, ParticleSimMode::Gpu);
}

TEST(ParticleSimMode, ForceCpuFalseLeavesGpuAliveCountAtZero)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc = cpuMotionDesc();
    desc.simMode      = ParticleSimMode::Gpu;
    desc.maxParticles = 512;
    desc.emissionRate = 1000.0f;
    emitter.setDesc(desc);
    emitter.setForceCpu(true);
    emitter.update(0.1f, false);
    EXPECT_FALSE(emitter.forceCpu());
    EXPECT_EQ(emitter.aliveCount(), 0u);
    EXPECT_EQ(emitter.capacity(), 0u);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Gpu);
}

TEST(ParticleSimMode, ForceCpuAboveCpuCapAllocatesNothing)
{
    g_overCapLogs = 0;
    LogCapture capture(captureParticleLogs);

    ParticleEmitter emitter;
    ParticleEmitterDesc desc{};
    desc.simMode      = ParticleSimMode::Gpu;
    desc.maxParticles = 200000;
    desc.emissionRate = 10000.0f;
    emitter.setDesc(desc);
    emitter.update(0.1f, true);
    emitter.update(0.1f, true);
    EXPECT_EQ(emitter.capacity(), 0u);
    EXPECT_EQ(emitter.aliveCount(), 0u);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Gpu);
    EXPECT_TRUE(emitter.forceCpu());
    EXPECT_EQ(g_overCapLogs, 1);
    emitter.emitBurst(4);
    EXPECT_EQ(emitter.capacity(), 0u);
    EXPECT_EQ(emitter.pendingBurst(), 0u);
}

TEST(ParticleSimMode, RibbonGpuSetDescBecomesCpu)
{
    g_ribbonLogs = 0;
    LogCapture capture(captureParticleLogs);

    ParticleEmitter emitter;
    ParticleEmitterDesc desc{};
    desc.simMode      = ParticleSimMode::Gpu;
    desc.renderMode   = ParticleEmitterDesc::RenderMode::Ribbon;
    desc.maxParticles = 32;
    emitter.setDesc(desc);
    emitter.setDesc(desc);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Cpu);
    EXPECT_EQ(emitter.desc().renderMode, ParticleEmitterDesc::RenderMode::Ribbon);
    EXPECT_EQ(emitter.capacity(), 32u);
    EXPECT_EQ(g_ribbonLogs, 1);
}

TEST(ParticleSimMode, SpawnWindow)
{
    ParticleSpawnStep step = particleAdvanceSpawn(0.0f, 0.0f, 1.0f, 8u, 3u, 0u);
    EXPECT_EQ(step.spawnCount, 0u);
    EXPECT_EQ(step.head, 3u);
    EXPECT_FLOAT_EQ(step.carry, 0.0f);
    for (uint32_t slot = 0; slot < 8u; ++slot)
        EXPECT_FALSE(particleSlotInSpawnWindow(slot, step.head, step.spawnCount, 8u));

    step = particleAdvanceSpawn(0.0f, 8.0f, 1.0f, 8u, 1u, 0u);
    EXPECT_EQ(step.spawnCount, 8u);
    EXPECT_EQ(step.head, 1u);
    EXPECT_FLOAT_EQ(step.carry, 0.0f);
    for (uint32_t slot = 0; slot < 8u; ++slot)
        EXPECT_TRUE(particleSlotInSpawnWindow(slot, step.head, step.spawnCount, 8u));

    step = particleAdvanceSpawn(0.25f, 100.0f, 1.0f, 8u, 2u, 0u);
    EXPECT_EQ(step.spawnCount, 8u);
    EXPECT_EQ(step.head, 2u);
    EXPECT_NEAR(step.carry, 0.25f, 1.0e-5f);
    for (uint32_t slot = 0; slot < 8u; ++slot)
        EXPECT_TRUE(particleSlotInSpawnWindow(slot, step.head, step.spawnCount, 8u));

    step = particleAdvanceSpawn(0.4f, 3.0f, 0.5f, 0u, 7u, 4u);
    EXPECT_EQ(step.head, 7u);
    EXPECT_EQ(step.spawnCount, 0u);
    EXPECT_FLOAT_EQ(step.carry, 0.0f);
    EXPECT_FALSE(particleSlotInSpawnWindow(0u, 7u, 1u, 0u));

    step = particleAdvanceSpawn(0.0f, 1.0f, 1.0f, 8u, 0u, 0xffffffffu);
    EXPECT_EQ(step.spawnCount, 8u);
    EXPECT_EQ(step.head, 0u);
    EXPECT_FLOAT_EQ(step.carry, 0.0f);

    step = particleAdvanceSpawn(0.0f, 2.0f, 1.0f, 8u, 3u, 0u);
    EXPECT_EQ(step.spawnCount, 2u);
    EXPECT_EQ(step.head, 5u);
    EXPECT_TRUE(particleSlotInSpawnWindow(3u, step.head, step.spawnCount, 8u));
    EXPECT_TRUE(particleSlotInSpawnWindow(4u, step.head, step.spawnCount, 8u));
    EXPECT_FALSE(particleSlotInSpawnWindow(5u, step.head, step.spawnCount, 8u));
    EXPECT_FALSE(particleSlotInSpawnWindow(2u, step.head, step.spawnCount, 8u));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    step = particleAdvanceSpawn(nan, 1.0f, 1.0f, 4u, 2u, 1u);
    EXPECT_EQ(step.head, 2u);
    EXPECT_EQ(step.spawnCount, 0u);
    EXPECT_FLOAT_EQ(step.carry, 0.0f);
}

TEST(ParticleSimMode, PoolDecisionTable)
{
    ParticlePoolDecision decision = particlePoolDecision(ParticleSimMode::Gpu, 200000u, false, true);
    EXPECT_FALSE(decision.forceCpu);
    EXPECT_FALSE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Gpu, 512u, false, true);
    EXPECT_TRUE(decision.forceCpu);
    EXPECT_FALSE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Gpu, kMaxCpuParticles, false, false);
    EXPECT_TRUE(decision.forceCpu);
    EXPECT_FALSE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Gpu, kMaxCpuParticles + 1u, true, false);
    EXPECT_FALSE(decision.forceCpu);
    EXPECT_FALSE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Gpu, 512u, true, true);
    EXPECT_FALSE(decision.forceCpu);
    EXPECT_TRUE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Gpu, 200000u, true, true);
    EXPECT_FALSE(decision.forceCpu);
    EXPECT_TRUE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Cpu, 512u, true, true);
    EXPECT_FALSE(decision.forceCpu);
    EXPECT_FALSE(decision.recordGpuPool);

    decision = particlePoolDecision(ParticleSimMode::Cpu, 200000u, false, false);
    EXPECT_FALSE(decision.forceCpu);
    EXPECT_FALSE(decision.recordGpuPool);
}

TEST(ParticleSimMode, ClearSerialBurstAndClock)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc{};
    desc.maxParticles = 8;
    emitter.setDesc(desc);
    emitter.emitBurst(2);
    EXPECT_EQ(emitter.aliveCount(), 2u);

    const uint32_t serial = emitter.emitCommand().clearSerial;
    emitter.noteGpuClock(0.4f, 2.0f, true);
    emitter.stop(false);
    EXPECT_EQ(emitter.emitCommand().clearSerial, serial);
    EXPECT_FALSE(emitter.emitCommand().playing);
    EXPECT_FLOAT_EQ(emitter.emitCommand().emitCarry, 0.0f);
    EXPECT_FLOAT_EQ(emitter.emitCommand().age, 2.0f);
    EXPECT_EQ(emitter.aliveCount(), 2u);

    emitter.stop(true);
    EXPECT_EQ(emitter.emitCommand().clearSerial, serial + 1u);
    EXPECT_EQ(emitter.aliveCount(), 0u);
    EXPECT_EQ(emitter.pendingBurst(), 0u);

    emitter.restart();
    EXPECT_EQ(emitter.emitCommand().clearSerial, serial + 2u);
    EXPECT_TRUE(emitter.isPlaying());
    EXPECT_FLOAT_EQ(emitter.emitCommand().age, 0.0f);

    ParticleEmitterDesc gpu = desc;
    gpu.simMode = ParticleSimMode::Gpu;
    gpu.maxParticles = 32;
    emitter.setDesc(gpu);
    EXPECT_EQ(emitter.capacity(), 0u);
    emitter.emitBurst(5);
    emitter.emitBurst(2);
    EXPECT_EQ(emitter.pendingBurst(), 7u);
    EXPECT_EQ(emitter.capacity(), 0u);
    EXPECT_EQ(emitter.aliveCount(), 0u);
    EXPECT_EQ(emitter.takeBurst(), 7u);
    EXPECT_EQ(emitter.pendingBurst(), 0u);

    emitter.setForceCpu(true);
    emitter.emitBurst(3);
    EXPECT_EQ(emitter.aliveCount(), 3u);
    EXPECT_EQ(emitter.capacity(), 32u);
    EXPECT_EQ(emitter.pendingBurst(), 0u);
    EXPECT_EQ(emitter.desc().simMode, ParticleSimMode::Gpu);
}

TEST(ParticleSimMode, ChildSpawnSecondaryIsStoredAsStop)
{
    ParticleEmitter emitter;
    ParticleEmitterDesc desc{};
    desc.hasSecondary = true;
    desc.secondary.collision = ParticleCollisionReaction::SpawnSecondary;
    desc.secondary.maxParticles = 1000000;
    desc.maxSecondaryPerFrame = 9000;
    emitter.setDesc(desc);
    EXPECT_EQ(emitter.desc().secondary.collision, ParticleCollisionReaction::Stop);
    EXPECT_EQ(emitter.desc().secondary.maxParticles, kMaxSecondaryParticles);
    EXPECT_EQ(emitter.desc().maxSecondaryPerFrame, kMaxSecondaryPerFrameHard);
}

TEST(ParticleSimMode, GpuDescGenerationBumpsOnModeAndRepeatsDoNot)
{
    ParticleEmitter emitter;
    const uint32_t before = emitter.gpuDescGeneration();
    ParticleEmitterDesc desc{};
    desc.simMode = ParticleSimMode::Gpu;
    desc.maxParticles = 128;
    emitter.setDesc(desc);
    const uint32_t once = emitter.gpuDescGeneration();
    EXPECT_GT(once, before);
    emitter.setDesc(emitter.desc());
    EXPECT_EQ(emitter.gpuDescGeneration(), once);
}

TEST(ParticleSimMode, TickStillUpdatesAndSkipsGpuIntegration)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f(4.0f, 0.0f, -1.0f);
    world.emplace<TransformComponent>(e, xf);

    ParticleEmitterComponent pe{};
    pe.desc.simMode = ParticleSimMode::Gpu;
    pe.desc.maxParticles = 200000;
    pe.desc.emissionRate = 1000.0f;
    pe.playing = true;
    world.emplace<ParticleEmitterComponent>(e, std::move(pe));

    tickParticleEmitters(world, 0.5f);
    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->runtime, nullptr);
    EXPECT_EQ(c->runtime->capacity(), 0u);
    EXPECT_EQ(c->runtime->aliveCount(), 0u);
    EXPECT_EQ(c->runtime->desc().simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(c->runtime->desc().maxParticles, 200000u);
    EXPECT_NEAR(c->runtime->position().x, 4.0f, 1.0e-4f);
}

TEST(ParticleSimMode, FlagOff512GpuDescRunsCpuEuler)
{
    World world;
    Entity e = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f(1.0f, 2.0f, 3.0f);
    world.emplace<TransformComponent>(e, xf);

    ParticleEmitterComponent pe{};
    pe.desc               = cpuMotionDesc();
    pe.desc.simMode       = ParticleSimMode::Gpu;
    pe.desc.maxParticles  = 512;
    pe.desc.emissionRate  = 1000.0f;
    pe.desc.startSpeed    = { 0.0f, 0.0f };
    pe.desc.gravity       = Vector3f(0.0f, -10.0f, 0.0f);
    pe.playing            = true;
    world.emplace<ParticleEmitterComponent>(e, std::move(pe));

    GpuParticleSystem gpu;
    const float dt = 0.1f;
    gpu.applyIntegratorFallback(world, false);
    tickParticleEmitters(world, dt);
    gpu.prepare(world, dt);

    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->runtime, nullptr);
    EXPECT_GT(c->runtime->aliveCount(), 0u);
    EXPECT_EQ(c->runtime->capacity(), 512u);
    EXPECT_TRUE(c->runtime->forceCpu());
    EXPECT_EQ(c->runtime->desc().simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(gpu.poolCount(), 0u);
    const Particle* spawned = firstSpawned(*c->runtime);
    ASSERT_NE(spawned, nullptr);
    EXPECT_NEAR(spawned->velocity.y, -10.0f * dt, 1.0e-4f);
    EXPECT_NEAR(spawned->position.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(spawned->position.y, 2.0f + (-10.0f * dt) * dt, 1.0e-4f);
    EXPECT_NEAR(spawned->position.z, 3.0f, 1.0e-4f);
}

TEST(ParticleSimMode, FlagOff200000CreatesNoPool)
{
    g_poolCreates   = 0;
    g_committedLogs = 0;
    g_frozenLogs    = 0;
    g_byteLogs      = 0;
    const uint64_t bytes = static_cast<uint64_t>(200000u) * sizeof(GpuParticle) * GpuParticleSystem::kFrameCount
        + static_cast<uint64_t>(GpuParticleSystem::kCbStride) * GpuParticleSystem::kFrameCount;
    const std::to_chars_result written = std::to_chars(g_bytesNeedle, g_bytesNeedle + sizeof(g_bytesNeedle) - 1, bytes);
    ASSERT_EQ(written.ec, std::errc{});
    *written.ptr = '\0';
    LogCapture capture(capturePoolLogs);

    World world;
    Entity e = addGpuEmitter(world, 200000u, 10000.0f);

    ParticleHookCounts counts;
    GpuParticleSystem gpu;
    ParticleHookGuard hook(&counts);
    Renderer renderer(RendererTestDevice{});
    renderer.debugState().gpuParticles = true;
    Camera3D camera;
    AssetManager assets;

    gpu.applyIntegratorFallback(world, true);
    tickParticleEmitters(world, 0.1f);
    gpu.prepare(world, 0.1f);
    const int committedOn = static_cast<int>(GpuParticleSystem::kFrameCount) + 1;
    EXPECT_EQ(counts.committed, committedOn);
    EXPECT_EQ(gpu.poolCount(), 1u);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_GT(counts.dispatches, 0);
    EXPECT_GT(counts.draws, 0);

    const int dispatches = counts.dispatches;
    const int draws      = counts.draws;
    renderer.debugState().gpuParticles = false;
    renderer.setFrameIndexForTest(1);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_EQ(counts.dispatches, dispatches);
    EXPECT_EQ(counts.draws, draws);
    EXPECT_EQ(counts.committed, committedOn);
    EXPECT_EQ(gpu.poolCount(), 1u);

    gpu.applyIntegratorFallback(world, false);
    gpu.prepare(world, 0.1f);
    gpu.prepare(world, 0.1f);

    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->runtime, nullptr);
    EXPECT_EQ(c->runtime->capacity(), 0u);
    EXPECT_EQ(c->runtime->aliveCount(), 0u);
    EXPECT_FALSE(c->runtime->forceCpu());
    EXPECT_EQ(c->runtime->desc().simMode, ParticleSimMode::Gpu);
    EXPECT_EQ(c->runtime->desc().maxParticles, 200000u);
    EXPECT_EQ(gpu.poolCount(), 0u);
    EXPECT_EQ(counts.committed, committedOn);
    EXPECT_EQ(counts.dispatches, dispatches);
    EXPECT_EQ(counts.draws, draws);
    EXPECT_EQ(g_poolCreates, 1);
    EXPECT_EQ(g_committedLogs, 0);
    EXPECT_EQ(g_frozenLogs, 1);
    EXPECT_EQ(g_byteLogs, 2);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_EQ(counts.dispatches, dispatches);
    EXPECT_EQ(counts.draws, draws);
    g_bytesNeedle[0] = '\0';
}

TEST(ParticleSimMode, LaterRefusalStillLogsOnce)
{
    g_refusedLogs      = 0;
    g_frozenLogs       = 0;
    g_createFailedLogs = 0;
    g_bytesNeedle[0]   = '\0';
    LogCapture capture(capturePoolLogs);

    World world;
    Entity e = addGpuEmitter(world, 512u, 10.0f);
    ParticleHookCounts counts;
    GpuParticleSystem gpu;
    ParticleHookGuard hook(&counts);

    gpu.applyIntegratorFallback(world, false);
    tickParticleEmitters(world, 0.1f);
    gpu.prepare(world, 0.1f);
    gpu.prepare(world, 0.1f);
    EXPECT_EQ(g_refusedLogs, 1);
    EXPECT_EQ(g_frozenLogs, 0);
    EXPECT_EQ(g_createFailedLogs, 0);
    EXPECT_EQ(counts.committed, 0);

    counts.failCreate = true;
    gpu.applyIntegratorFallback(world, true);
    gpu.prepare(world, 0.1f);
    gpu.prepare(world, 0.1f);
    EXPECT_EQ(g_refusedLogs, 1);
    EXPECT_EQ(g_createFailedLogs, 1);
    EXPECT_EQ(counts.committed, 0);

    ParticleEmitterComponent* c = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->runtime, nullptr);
    ParticleEmitterDesc desc = c->runtime->desc();
    desc.maxParticles        = 200000u;
    c->runtime->setDesc(desc);
    counts.failCreate = false;
    gpu.applyIntegratorFallback(world, false);
    gpu.prepare(world, 0.1f);
    gpu.prepare(world, 0.1f);
    EXPECT_EQ(g_refusedLogs, 1);
    EXPECT_EQ(g_createFailedLogs, 1);
    EXPECT_EQ(g_frozenLogs, 1);
    EXPECT_EQ(counts.committed, 0);
    EXPECT_FALSE(c->runtime->forceCpu());
    EXPECT_EQ(c->runtime->desc().simMode, ParticleSimMode::Gpu);
}

TEST(ParticleSimMode, BufferStepInitializesOnceThenReadsLastSlot)
{
    const GpuParticleBufferStep born = gpuParticleBufferStep(1u, -1, 0u);
    EXPECT_TRUE(born.reset);
    EXPECT_TRUE(born.inPlace);

    const GpuParticleBufferStep kept = gpuParticleBufferStep(0u, 0, 1u);
    EXPECT_FALSE(kept.reset);
    EXPECT_FALSE(kept.inPlace);

    const GpuParticleBufferStep skipped = gpuParticleBufferStep(0u, 0, 0u);
    EXPECT_FALSE(skipped.reset);
    EXPECT_TRUE(skipped.inPlace);

    const GpuParticleBufferStep secondReset = gpuParticleBufferStep(1u, 0, 1u);
    EXPECT_TRUE(secondReset.reset);
    EXPECT_TRUE(secondReset.inPlace);
}

TEST(ParticleSimMode, BurstResetsOnceThenPreservesAcrossPingPong)
{
    World world;
    Entity e = addGpuEmitter(world, 64u, 0.0f);
    ParticleEmitterComponent* created = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(created, nullptr);
    ensureParticleRuntime(*created);
    ASSERT_NE(created->runtime, nullptr);
    created->runtime->emitBurst(8);

    ParticleHookCounts counts;
    GpuParticleSystem gpu;
    ParticleHookGuard hook(&counts);
    Renderer renderer(RendererTestDevice{});
    renderer.debugState().gpuParticles = true;
    Camera3D camera;
    AssetManager assets;

    const float dt = 1.0f / 60.0f;
    gpu.applyIntegratorFallback(world, true);
    gpu.prepare(world, dt);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_EQ(counts.dispatches, 1);
    EXPECT_EQ(counts.resetDispatches, 1);
    EXPECT_EQ(counts.preserveDispatches, 0);
    EXPECT_EQ(counts.lastDispatchTag, kGpuParticleTestReset | kGpuParticleTestInPlace);

    renderer.setFrameIndexForTest(1);
    gpu.prepare(world, dt);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_EQ(counts.dispatches, 2);
    EXPECT_EQ(counts.resetDispatches, 1);
    EXPECT_EQ(counts.preserveDispatches, 1);
    EXPECT_EQ(counts.lastDispatchTag, 0u);

    ParticleEmitterComponent* live = world.get<ParticleEmitterComponent>(e);
    ASSERT_NE(live, nullptr);
    ASSERT_NE(live->runtime, nullptr);
    live->runtime->stop(true);
    live->runtime->play();
    live->runtime->emitBurst(4);

    renderer.setFrameIndexForTest(2);
    gpu.prepare(world, dt);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_EQ(counts.dispatches, 3);
    EXPECT_EQ(counts.resetDispatches, 2);
    EXPECT_EQ(counts.preserveDispatches, 1);
    EXPECT_EQ(counts.lastDispatchTag, kGpuParticleTestReset | kGpuParticleTestInPlace);

    renderer.setFrameIndexForTest(3);
    gpu.prepare(world, dt);
    ASSERT_TRUE(gpu.simulateAndDraw(nullptr, renderer, camera, world, assets, false));
    EXPECT_EQ(counts.dispatches, 4);
    EXPECT_EQ(counts.resetDispatches, 2);
    EXPECT_EQ(counts.preserveDispatches, 2);
    EXPECT_EQ(counts.lastDispatchTag, 0u);
}

TEST(ParticleSimMode, GpuSubstepCountClampsDt)
{
    EXPECT_EQ(GpuParticleSystem::substepCount(1.0f / 60.0f, 1.0f), 1u);
    EXPECT_EQ(GpuParticleSystem::substepCount(1.0f / 30.0f, 1.0f), 2u);
    EXPECT_EQ(GpuParticleSystem::substepCount(0.1f, 1.0f), 2u);
    EXPECT_NEAR(GpuParticleSystem::clampedDt(0.1f, 1.0f), GpuParticleSystem::kMaxDt, 1.0e-6f);
    EXPECT_NEAR(GpuParticleSystem::clampedDt(1.0f / 60.0f, 1.0f), 1.0f / 60.0f, 1.0e-6f);
    EXPECT_EQ(GpuParticleSystem::substepCount(1.0f / 120.0f, 2.0f), 1u);
    EXPECT_EQ(GpuParticleSystem::substepCount(0.0f, 1.0f), 0u);
}

TEST(ParticleSimMode, GpuBillboardShadersCompile)
{
    Microsoft::WRL::ComPtr<ID3DBlob> csCopy;
    Microsoft::WRL::ComPtr<ID3DBlob> csInPlace;
    Microsoft::WRL::ComPtr<ID3DBlob> vs;
    Microsoft::WRL::ComPtr<ID3DBlob> ps;
    ASSERT_TRUE(compileShaderFromContent("shaders/ParticleSimulate.hlsl", "CSSimulate", "cs_5_0", csCopy));
    ASSERT_TRUE(compileShaderFromContent("shaders/ParticleSimulate.hlsl", "CSInPlace", "cs_5_0", csInPlace));
    D3D_SHADER_MACRO macros[2]{};
    makeEncodeSrgbMacros(false, macros);
    ASSERT_TRUE(compileShaderFromContent("shaders/ParticleBillboard.hlsl", "VSMain", "vs_5_0", vs, macros));
    ASSERT_TRUE(compileShaderFromContent("shaders/Particle.hlsl", "PSMain", "ps_5_0", ps, macros));
    makeEncodeSrgbMacros(true, macros);
    ASSERT_TRUE(compileShaderFromContent("shaders/ParticleBillboard.hlsl", "VSMain", "vs_5_0", vs, macros));
    ASSERT_TRUE(compileShaderFromContent("shaders/Particle.hlsl", "PSMain", "ps_5_0", ps, macros));
}
