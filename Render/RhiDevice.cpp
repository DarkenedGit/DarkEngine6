#include "Render/RhiDevice.h"
#include "Core/Log.h"
#include "Core/Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <filesystem>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    namespace
    {
        std::string narrow(const wchar_t* s)
        {
            if (!s || !*s)
                return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string out(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
            return out;
        }

        std::wstring widen(const std::string& s)
        {
            if (s.empty())
                return {};
            const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
            return out;
        }

        uint32_t maxFeatureLevel(ID3D12Device* device)
        {
            const D3D_FEATURE_LEVEL           levels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
            D3D12_FEATURE_DATA_FEATURE_LEVELS data{};
            data.NumFeatureLevels        = static_cast<UINT>(sizeof(levels) / sizeof(levels[0]));
            data.pFeatureLevelsRequested = levels;
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data))))
                return 0;
            return static_cast<uint32_t>(data.MaxSupportedFeatureLevel);
        }

        uint32_t highestShaderModel(ID3D12Device* device)
        {
            // Ask for the newest model this header knows; runtimes that do not know it return E_INVALIDARG.
            D3D12_FEATURE_DATA_SHADER_MODEL sm{ D3D_HIGHEST_SHADER_MODEL };
            while (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))))
            {
                if (sm.HighestShaderModel <= D3D_SHADER_MODEL_5_1)
                    return 0;
                sm.HighestShaderModel = sm.HighestShaderModel == D3D_SHADER_MODEL_6_0 ? D3D_SHADER_MODEL_5_1 : static_cast<D3D_SHADER_MODEL>(sm.HighestShaderModel - 1);
            }
            return static_cast<uint32_t>(sm.HighestShaderModel);
        }

        bool computeQueueTimestamps(ID3D12Device* device)
        {
            D3D12_COMMAND_QUEUE_DESC desc{};
            desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            ComPtr<ID3D12CommandQueue> queue;
            if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))))
                return false;
            UINT64 frequency = 0;
            return SUCCEEDED(queue->GetTimestampFrequency(&frequency)) && frequency != 0;
        }

        bool envValue(const wchar_t* name, std::string& out)
        {
            wchar_t     buf[64]{};
            const DWORD n = GetEnvironmentVariableW(name, buf, 64);
            if (n == 0 || n >= 64)
                return false;
            out = narrow(buf);
            return true;
        }

        void applyFakeFromEnvironment(StartupGateInput& in)
        {
            std::string id;
            if (!envValue(L"DE_STARTUP_GATE_FAKE", id))
                return;
            const StartupGateCheck check = startupGateCheckFromId(id.c_str());
            if (check == StartupGateCheck::None)
            {
                DE_LOG_WARN(LogCategory::Render, "DE_STARTUP_GATE_FAKE='{}' is not one of os, agility, fl, sm, binding, barriers, driver", id);
                return;
            }
            DE_LOG_WARN(LogCategory::Render, "DE_STARTUP_GATE_FAKE: failing the '{}' check on purpose", id);
            applyStartupGateFake(check, in);
        }

        ComPtr<IDXGIAdapter1> pickHardwareAdapter(IDXGIFactory6* factory)
        {
            ComPtr<IDXGIAdapter1> adapter;
            for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
            {
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
                    return adapter;
                adapter.Reset();
            }
            return {};
        }
    } // namespace

    bool queryOsVersion(uint32_t& major, uint32_t& minor, uint32_t& build)
    {
        major = minor = build = 0;
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const HMODULE ntdll   = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll)
            return false;
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (!rtlGetVersion)
            return false;
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        if (rtlGetVersion(&info) != 0)
            return false;
        major = info.dwMajorVersion;
        minor = info.dwMinorVersion;
        build = info.dwBuildNumber;
        return true;
    }

    AgilityStatus queryAgilityStatus()
    {
        AgilityStatus status;
        if (const auto* exported = reinterpret_cast<const UINT*>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(nullptr), "D3D12SDKVersion"))))
            status.sdkVersion = *exported;

        const HMODULE core = GetModuleHandleW(L"D3D12Core.dll");
        if (!core)
            return status;
        wchar_t     path[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(core, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return status;
        status.corePath = narrow(path);

        std::error_code             ec;
        const std::filesystem::path expected = std::filesystem::weakly_canonical(executableDirectory() / L"D3D12" / L"D3D12Core.dll", ec);
        const std::filesystem::path loaded   = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
        status.loaded                        = !expected.empty() && _wcsicmp(expected.c_str(), loaded.c_str()) == 0;
        return status;
    }

    bool queryRhiCaps(ID3D12Device* device, IDXGIAdapter1* adapter, RhiCaps& out)
    {
        out = {};
        if (!device)
            return false;

        if (adapter)
        {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)))
            {
                out.adapterName          = narrow(desc.Description);
                out.vendorId             = desc.VendorId;
                out.deviceId             = desc.DeviceId;
                out.dedicatedVideoMemory = desc.DedicatedVideoMemory;
                out.software             = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            }
            LARGE_INTEGER umd{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
                out.umdVersion = static_cast<uint64_t>(umd.QuadPart);
        }

        out.featureLevel = maxFeatureLevel(device);
        out.shaderModel  = highestShaderModel(device);

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        {
            out.bindingTier = options.ResourceBindingTier;
            out.heapTier    = options.ResourceHeapTier;
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))))
        {
            out.waveOps          = options1.WaveOps != FALSE;
            out.waveLaneCountMin = options1.WaveLaneCountMin;
            out.waveLaneCountMax = options1.WaveLaneCountMax;
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12))))
            out.enhancedBarriers = options12.EnhancedBarriersSupported != FALSE;

        out.computeTimestamps = computeQueueTimestamps(device);
        return true;
    }

    void logRhiCaps(const RhiCaps& caps, const AgilityStatus& agility)
    {
        DE_LOG_INFO(LogCategory::Render, "D3D12 device: {} ({} 0x{:04X}:0x{:04X}, {} MB{})", caps.adapterName, vendorName(caps.vendorId), caps.vendorId, caps.deviceId,
                    caps.dedicatedVideoMemory / (1024ull * 1024ull), caps.software ? ", software" : "");
        DE_LOG_INFO(LogCategory::Render, "Agility SDK: D3D12SDKVersion {}, D3D12Core.dll {} ({})", agility.sdkVersion, agility.corePath.empty() ? std::string("not loaded") : agility.corePath,
                    agility.loaded ? "Agility" : "not the Agility runtime");
        DE_LOG_INFO(LogCategory::Render, "RHI caps: FL {}, SM {}, binding tier {}, heap tier {}, enhanced barriers {}, wave ops {} ({}-{} lanes), compute timestamps {}, driver {}",
                    formatFeatureLevel(caps.featureLevel), formatShaderModel(caps.shaderModel), static_cast<int>(caps.bindingTier), static_cast<int>(caps.heapTier),
                    caps.enhancedBarriers ? "yes" : "no", caps.waveOps ? "yes" : "no", caps.waveLaneCountMin, caps.waveLaneCountMax, caps.computeTimestamps ? "yes" : "no",
                    formatDriverVersion(caps.vendorId, caps.umdVersion));
    }

    StartupGateInput makeStartupGateInput(uint32_t osMajor, uint32_t osMinor, uint32_t osBuild, const AgilityStatus& agility, const RhiCaps& caps)
    {
        StartupGateInput in;
        in.osMajor           = osMajor;
        in.osMinor           = osMinor;
        in.osBuild           = osBuild;
        in.agilityLoaded     = agility.loaded;
        in.agilityPath       = agility.corePath;
        in.agilitySdkVersion = agility.sdkVersion;
        in.adapterName       = caps.adapterName;
        in.vendorId          = caps.vendorId;
        in.featureLevel      = caps.featureLevel;
        in.shaderModel       = caps.shaderModel;
        in.bindingTier       = static_cast<uint32_t>(caps.bindingTier);
        in.enhancedBarriers  = caps.enhancedBarriers;
        in.umdVersion        = caps.umdVersion;
        in.skipDriverCheck   = caps.software;
        return in;
    }

    bool createRhiDevice(IDXGIFactory6* factory, RhiDeviceResult& out)
    {
        out              = {};
        uint32_t osMajor = 0, osMinor = 0, osBuild = 0;
        if (!queryOsVersion(osMajor, osMinor, osBuild))
            DE_LOG_WARN(LogCategory::Render, "RtlGetVersion unavailable; the Windows 11 check sees build 0");

        // Windows first, before touching D3D12 (plan §3.15 gate order).
        {
            StartupGateInput osOnly = makeStartupGateInput(osMajor, osMinor, osBuild, AgilityStatus{ true, {}, 0 }, RhiCaps{});
            applyFakeFromEnvironment(osOnly);
            if (osOnly.osBuild < kGateMinWindowsBuild)
            {
                out.gate = evaluateStartupGate(osOnly);
                return false;
            }
        }

        if (factory)
            out.adapter = pickHardwareAdapter(factory);
        if (out.adapter)
        {
            if (FAILED(D3D12CreateDevice(out.adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&out.device))))
            {
                // Below FL 12_1: create at 11_0 only to read the caps for the message.
                out.device.Reset();
                if (FAILED(D3D12CreateDevice(out.adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&out.device))))
                    out.device.Reset();
            }
        }

        if (out.device)
            queryRhiCaps(out.device.Get(), out.adapter.Get(), out.caps);
        else if (out.adapter)
        {
            DXGI_ADAPTER_DESC1 desc{};
            out.adapter->GetDesc1(&desc);
            out.caps.adapterName = narrow(desc.Description);
            out.caps.vendorId    = desc.VendorId;
        }
        else
            out.caps.adapterName = "no hardware D3D12 adapter";

        out.agility = queryAgilityStatus();
        logRhiCaps(out.caps, out.agility);

        StartupGateInput in = makeStartupGateInput(osMajor, osMinor, osBuild, out.agility, out.caps);
        // With no adapter, d3d12.dll never loaded D3D12Core.dll; report the missing GPU (FL check), not Agility.
        if (!out.adapter)
            in.agilityLoaded = true;
        applyFakeFromEnvironment(in);
        out.gate = evaluateStartupGate(in);
        if (!out.gate.passed())
        {
            out.device.Reset();
            return false;
        }
        DE_LOG_INFO(LogCategory::Render, "Startup gate passed (Windows build {}, FL {}, SM {}, binding tier {}, enhanced barriers, driver {})", osBuild, formatFeatureLevel(out.caps.featureLevel),
                    formatShaderModel(out.caps.shaderModel), static_cast<int>(out.caps.bindingTier), formatDriverVersion(out.caps.vendorId, out.caps.umdVersion));
        return true;
    }

    void reportStartupGateFailure(const StartupGateResult& gate)
    {
        DE_LOG_FATAL(LogCategory::Render, "Startup gate failed ({}): {}", startupGateCheckId(gate.failed), gate.message);
        const std::wstring text = widen(gate.message + "\n\nSee DarkEngine6.log for details.");
        MessageBoxW(nullptr, text.c_str(), L"DarkEngine6 - unsupported system", MB_OK | MB_ICONERROR);
    }

    ComPtr<ID3D12Device> createWarpTestDevice(RhiCaps* outCaps)
    {
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return {};
        ComPtr<IDXGIAdapter1> warp;
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
            return {};
        ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device))))
            return {};
        if (outCaps)
            queryRhiCaps(device.Get(), warp.Get(), *outCaps);
        return device;
    }

} // namespace Dark
