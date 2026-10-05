#pragma once

#include "ECS/Entity.h"
#include "Math/Matrix4f.h"
#include "Render/Frustum3f.h"
#include "Render/LocalShadowMath.h"

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Camera3D;
class ShadowPipeline;
class World;

using Microsoft::WRL::ComPtr;

struct LocalShadowFaceDraw
{
    Math::Matrix4f viewProj;
    Frustum3f      frustum;
    int            lightOrdinal = 0; // 0..3
    int            faceInLight  = 0; // 0 for a spot, 0..5 for a point
    bool           firstFace    = false; // foliage allocates its view here
    Entity         light        = {};
};

class LocalShadowSystem
{
public:
    LocalShadowSystem() = default;

    bool create(ID3D12Device* device, const LocalShadowSettings& settings = {});

    // select == false still latches frameIndex and writes no records (recordFor stays -1).
    // debugEnabled() == false does the same. This system does not read ShadowSystem.
    // frameIndex must be Renderer::frameIndex().
    void update(World& world, const Camera3D& camera, const Frustum3f& cameraFrustum, uint32_t frameIndex, bool select);

    void beginCapture(ID3D12GraphicsCommandList* cmd);
    void beginFace(ID3D12GraphicsCommandList* cmd, int faceOrdinal);
    void endCapture(ID3D12GraphicsCommandList* cmd);

    // beginFace binds this pipeline. ShadowSystem owns it; the pointer is not owned here.
    void setDepthPipeline(const ShadowPipeline* pipeline) { m_depthPipeline = pipeline; }

    // (frameIndex % 2) * 4 + ordinal, or -1. frameIndex is the value update latched.
    int  recordFor(Entity e) const;
    int  faceCountThisFrame() const { return m_faceCount; }
    const LocalShadowFaceDraw& face(int i) const;

    struct CpuSrvs
    {
        D3D12_CPU_DESCRIPTOR_HANDLE arraySrv   = {};
        D3D12_CPU_DESCRIPTOR_HANDLE recordsSrv = {};
    };
    CpuSrvs cpuSrvs() const { return m_cpu; }
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu() const { return m_cpu.arraySrv; }
    float debugSlice(int faceOrdinal) const;

    bool isValid() const { return m_resource != nullptr && m_records != nullptr; }
    bool debugEnabled() const { return m_debugEnabled; }
    void setDebugEnabled(bool enabled) { m_debugEnabled = enabled; }

    const LocalShadowSettings& settings() const { return m_settings; }
    int slicesPerFrame() const { return static_cast<int>(m_settings.slicesPerFrame); }

private:
    bool createArray(ID3D12Device* device);
    bool createRecords(ID3D12Device* device);
    bool createDummy(ID3D12Device* device);
    void clearSelection();
    void writeRecords(World& world);
    void closeProfileFace(ID3D12GraphicsCommandList* cmd);
    UINT sliceIndex(int faceInFrame) const;

    LocalShadowSettings m_settings{};
    const ShadowPipeline* m_depthPipeline = nullptr;

    std::array<LocalShadowFaceDraw, kLocalShadowSlicesPerFrame> m_faces{};
    int m_faceCount = 0;

    struct Slot
    {
        EntityID id      = 0;
        int      ordinal = 0;
    };
    std::array<Slot, kMaxShadowedLocalLights> m_slots{};
    int m_slotCount = 0;

    std::array<EntityID, kMaxShadowedLocalLights> m_held{};
    int m_heldCount = 0;
    std::array<EntityID, kMaxShadowedLocalLights> m_logged{};
    int m_loggedCount = 0;

    uint32_t m_frameIndex      = 0;
    int      m_frame           = 0;
    bool     m_haveUpdate      = false;
    bool     m_debugEnabled    = true;
    bool     m_createAttempted = false;
    bool     m_createOk        = false;
    bool     m_loggedReject    = false;
    bool     m_profileCapture  = false;
    bool     m_profileFace     = false;

    ComPtr<ID3D12Resource>       m_resource;
    ComPtr<ID3D12Resource>       m_records;
    ComPtr<ID3D12Resource>       m_dummy;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dummyHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_dsvCpu[kLocalShadowFrames][kLocalShadowSlicesPerFrame]{};
    CpuSrvs                      m_cpu{};
    UINT                         m_dsvIncr = 0;
    uint8_t*                     m_recordsMap = nullptr;
    D3D12_RESOURCE_STATES        m_state[kLocalShadowFrames]{
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_WRITE
    };
};

} // namespace Dark
