#pragma once

#include <cstdint>
#include <string>

// Startup gate for the platform minimum (plan D5, D9, D13; §3.15).
// Pure data and checks, no D3D12 or Win32 types, so the rules and messages are unit-tested natively.
// RhiDevice fills StartupGateInput from the OS, the loaded runtime and the device.

namespace Dark
{

    // Order matches the plan: the first failing check is the one reported.
    enum class StartupGateCheck : uint8_t
    {
        None = 0,
        Windows11,
        AgilityLoaded,
        FeatureLevel,
        ShaderModel,
        BindingTier,
        EnhancedBarriers,
        DriverVersion,
    };

    inline constexpr uint32_t kGateMinWindowsBuild = 22000;  // Windows 11 21H2
    inline constexpr uint32_t kGateMinFeatureLevel = 0xc100; // D3D_FEATURE_LEVEL_12_1
    inline constexpr uint32_t kGateMinShaderModel  = 0x66;   // D3D_SHADER_MODEL_6_6
    inline constexpr uint32_t kGateMinBindingTier  = 3;      // D3D12_RESOURCE_BINDING_TIER_3

    inline constexpr uint32_t kPciVendorNvidia = 0x10DE;
    inline constexpr uint32_t kPciVendorAmd    = 0x1002;
    inline constexpr uint32_t kPciVendorIntel  = 0x8086;

    // Minimum NVIDIA driver, as the marketing number times 100 (572.16 -> 57216).
    // R570 (Jan 2025) is the oldest branch assumed for the RTX 2080 with Agility 1.620 enhanced barriers.
    // Travis confirms or raises it on the reference machine (plan §3.7 [I]).
    inline constexpr uint32_t kMinNvidiaDriver = 57216;

    struct StartupGateInput
    {
        // OS (RtlGetVersion).
        uint32_t osMajor = 0;
        uint32_t osMinor = 0;
        uint32_t osBuild = 0;

        // Agility runtime: D3D12Core.dll loaded from the exe's D3D12\ folder.
        bool        agilityLoaded = false;
        std::string agilityPath;           // loaded D3D12Core.dll, empty when none
        uint32_t    agilitySdkVersion = 0; // D3D12SDKVersion exported by the exe

        // Adapter and device caps. featureLevel and shaderModel use the D3D enum values (0xc100, 0x66).
        std::string adapterName;
        uint32_t    vendorId         = 0;
        uint32_t    featureLevel     = 0;
        uint32_t    shaderModel      = 0;
        uint32_t    bindingTier      = 0;
        bool        enhancedBarriers = false;
        uint64_t    umdVersion       = 0; // LARGE_INTEGER from IDXGIAdapter::CheckInterfaceSupport

        // WARP and other software adapters skip the driver check (plan §3.15, unit tests).
        bool skipDriverCheck = false;
    };

    struct StartupGateResult
    {
        StartupGateCheck failed = StartupGateCheck::None;
        std::string      message; // empty when passed; otherwise names the GPU, found value, minimum and reason

        bool passed() const
        {
            return failed == StartupGateCheck::None;
        }
    };

    StartupGateResult evaluateStartupGate(const StartupGateInput& in);

    // Short id used by DE_STARTUP_GATE_FAKE and in logs: "os", "agility", "fl", "sm", "binding", "barriers", "driver".
    const char*      startupGateCheckId(StartupGateCheck check);
    StartupGateCheck startupGateCheckFromId(const char* id);

    // Makes `check` fail in `in` by replacing the observed value with one below the minimum.
    // Used by DE_STARTUP_GATE_FAKE so each message box can be checked on a machine that passes.
    void applyStartupGateFake(StartupGateCheck check, StartupGateInput& in);

    // Minimum driver for the vendor, as marketing number x100, or 0 when no minimum is pinned.
    uint32_t minimumDriverForVendor(uint32_t vendorId);

    // NVIDIA marketing number x100 from the UMD version (32.0.15.7216 -> 57216).
    uint32_t nvidiaDriverFromUmd(uint64_t umdVersion);

    // "32.0.15.7216".
    std::string formatUmdVersion(uint64_t umdVersion);

    // "572.16" for NVIDIA, the UMD quad otherwise.
    std::string formatDriverVersion(uint32_t vendorId, uint64_t umdVersion);

    // "12_1", "11_0".
    std::string formatFeatureLevel(uint32_t featureLevel);

    // "6.6".
    std::string formatShaderModel(uint32_t shaderModel);

    const char* vendorName(uint32_t vendorId);

} // namespace Dark
