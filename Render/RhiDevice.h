#pragma once

#include "Render/StartupGate.h"

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>
#include <wrl/client.h>

// Device creation and the platform startup gate (plan D4, D5, D9, D13; §3.15).
// Hosts use createRhiDevice; unit tests use createWarpTestDevice (same caps query, no driver check).

namespace Dark
{

    struct RhiCaps
    {
        std::string                 adapterName;
        uint32_t                    vendorId             = 0;
        uint32_t                    deviceId             = 0;
        uint64_t                    dedicatedVideoMemory = 0;
        bool                        software             = false;
        uint32_t                    featureLevel         = 0; // D3D_FEATURE_LEVEL value, e.g. 0xc100
        uint32_t                    shaderModel          = 0; // D3D_SHADER_MODEL value, e.g. 0x66
        D3D12_RESOURCE_BINDING_TIER bindingTier          = D3D12_RESOURCE_BINDING_TIER_1;
        D3D12_RESOURCE_HEAP_TIER    heapTier             = D3D12_RESOURCE_HEAP_TIER_1;
        bool                        enhancedBarriers     = false;
        bool                        waveOps              = false;
        uint32_t                    waveLaneCountMin     = 0;
        uint32_t                    waveLaneCountMax     = 0;
        bool                        computeTimestamps    = false; // compute queue reports a timestamp frequency
        uint64_t                    umdVersion           = 0;     // user-mode driver version (IDXGIAdapter::CheckInterfaceSupport)
    };

    struct AgilityStatus
    {
        bool        loaded = false; // D3D12Core.dll came from <exe dir>/D3D12/
        std::string corePath;       // loaded D3D12Core.dll, empty when none is loaded
        uint32_t    sdkVersion = 0; // D3D12SDKVersion exported by the exe (Core/AgilityExports.cpp)
    };

    // RtlGetVersion (the real build; GetVersionEx lies without a manifest). False if ntdll has no export.
    bool queryOsVersion(uint32_t& major, uint32_t& minor, uint32_t& build);

    // Call after the first device is created (that is when d3d12.dll loads D3D12Core.dll).
    AgilityStatus queryAgilityStatus();

    // Fills caps from the device and its adapter. Returns false if the device is null.
    bool queryRhiCaps(ID3D12Device* device, IDXGIAdapter1* adapter, RhiCaps& out);

    void logRhiCaps(const RhiCaps& caps, const AgilityStatus& agility);

    StartupGateInput makeStartupGateInput(uint32_t osMajor, uint32_t osMinor, uint32_t osBuild, const AgilityStatus& agility, const RhiCaps& caps);

    struct RhiDeviceResult
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        Microsoft::WRL::ComPtr<ID3D12Device>  device;
        RhiCaps                               caps;
        AgilityStatus                         agility;
        StartupGateResult                     gate;
    };

    // Picks the high-performance hardware adapter, creates the device at FL 12_1, fills the caps and runs
    // the startup gate (Windows 11, Agility loaded, FL 12_1, SM 6.6, Binding Tier 3, enhanced barriers,
    // minimum driver). There is no WARP or lower-tier fallback. On failure out.gate says why and
    // out.device is reset; the caller reports it with reportStartupGateFailure.
    // DE_STARTUP_GATE_FAKE=<os|agility|fl|sm|binding|barriers|driver> makes that check fail on purpose.
    bool createRhiDevice(IDXGIFactory6* factory, RhiDeviceResult& out);

    // Logs the reason (DE_LOG_FATAL) and shows it in a message box.
    void reportStartupGateFailure(const StartupGateResult& gate);

    // WARP device at FL 12_1 for unit tests, through the same caps query. The driver check is skipped
    // (plan §3.15). Null when WARP cannot create one. outCaps is optional.
    Microsoft::WRL::ComPtr<ID3D12Device> createWarpTestDevice(RhiCaps* outCaps = nullptr);

} // namespace Dark
