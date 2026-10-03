#pragma once

#include <cstdint>

struct ID3D12GraphicsCommandList;

namespace Dark
{

// ARGB colors shared by PIX (GPU + CPU) and NVTX (CPU). PIX, Nsight Graphics, and
// Nsight Systems all read the GPU events. NVTX is what Nsight shows on the CPU.
namespace ProfileColor
{
constexpr uint32_t Frame            = 0xFF9AA0A6;
constexpr uint32_t EcsUpdate        = 0xFF34A853;
constexpr uint32_t Shadows          = 0xFF6B4C9A;
constexpr uint32_t ShadowCascade    = 0xFF8E6CC0;
constexpr uint32_t GBuffer          = 0xFF1E8E3E;
constexpr uint32_t ForwardOpaque    = 0xFF43A047;
constexpr uint32_t Terrain          = 0xFF6B8E23;
constexpr uint32_t TerrainDepth     = 0xFF3D5C1E;
constexpr uint32_t OpaqueMeshes     = 0xFF00897B;
constexpr uint32_t OpaqueModels     = 0xFF00ACC1;
constexpr uint32_t Projectiles      = 0xFFEF6C00;
constexpr uint32_t DeferredLighting = 0xFFF9A825;
constexpr uint32_t LocalLights      = 0xFFFF7043;
constexpr uint32_t Sky              = 0xFF1E88E5;
constexpr uint32_t Water            = 0xFF0277BD;
constexpr uint32_t Clouds           = 0xFF81D4FA;
constexpr uint32_t Camouflage       = 0xFF7CB342;
constexpr uint32_t Translucent      = 0xFFEC407A;
constexpr uint32_t Particles        = 0xFFD81B60;
constexpr uint32_t Blood            = 0xFFC62828;
constexpr uint32_t Post             = 0xFFECEFF1;
constexpr uint32_t AutoExposure     = 0xFFFFD54F;
constexpr uint32_t Bloom            = 0xFFFFEE58;
constexpr uint32_t Taa              = 0xFFB39DDB;
constexpr uint32_t MotionBlur       = 0xFF7E57C2;
constexpr uint32_t Tonemap          = 0xFFFFFFFF;
constexpr uint32_t Gtao             = 0xFF8D6E63;
constexpr uint32_t Ssr              = 0xFF546E7A;
constexpr uint32_t Hud              = 0xFFE53935;
constexpr uint32_t ImGui            = 0xFF5C6BC0;
constexpr uint32_t DebugOverlay     = 0xFF78909C;
constexpr uint32_t Menu             = 0xFF8E24AA;
constexpr uint32_t Loading          = 0xFF26A69A;
constexpr uint32_t Sprites          = 0xFF26C6DA;
constexpr uint32_t Background       = 0xFF4FC3F7;
constexpr uint32_t Grid             = 0xFF90A4AE;
constexpr uint32_t Gizmos           = 0xFFFFB74D;
constexpr uint32_t Skeleton         = 0xFFAED581;
constexpr uint32_t Collision        = 0xFFFFA726;
} // namespace ProfileColor

// Call once after the device exists so the first frame does not pay NVTX init.
void profileStartup();

void profileCpuBegin(const char* name, uint32_t argb);
void profileCpuEnd();
void profileGpuBegin(ID3D12GraphicsCommandList* cmd, const char* name, uint32_t argb);
void profileGpuEnd(ID3D12GraphicsCommandList* cmd);

class CpuScope
{
public:
    explicit CpuScope(const char* name, uint32_t argb = ProfileColor::EcsUpdate)
    {
        if (!name || !name[0])
            return;
        profileCpuBegin(name, argb);
        m_open = true;
    }

    ~CpuScope()
    {
        if (m_open)
            profileCpuEnd();
    }

    CpuScope(const CpuScope&)            = delete;
    CpuScope& operator=(const CpuScope&) = delete;

private:
    bool m_open = false;
};

// CPU range for the recording cost, plus a command-list event for the GPU timeline.
class GpuScope
{
public:
    GpuScope(ID3D12GraphicsCommandList* cmd, const char* name, uint32_t argb = ProfileColor::Frame)
    {
        if (!cmd || !name || !name[0])
            return;
        profileCpuBegin(name, argb);
        profileGpuBegin(cmd, name, argb);
        m_cmd = cmd;
    }

    ~GpuScope()
    {
        if (!m_cmd)
            return;
        profileGpuEnd(m_cmd);
        profileCpuEnd();
    }

    GpuScope(const GpuScope&)            = delete;
    GpuScope& operator=(const GpuScope&) = delete;

private:
    ID3D12GraphicsCommandList* m_cmd = nullptr;
};

} // namespace Dark
