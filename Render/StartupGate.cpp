#include "Render/StartupGate.h"

#include <cstring>
#include <format>
#include <utility>

namespace Dark
{

    namespace
    {
        std::string gpuBlock(const StartupGateInput& in)
        {
            const std::string name = in.adapterName.empty() ? std::string("unknown") : in.adapterName;
            return std::format("GPU: {} ({})\nDriver: {}\n", name, vendorName(in.vendorId), formatDriverVersion(in.vendorId, in.umdVersion));
        }

        StartupGateResult fail(StartupGateCheck check, std::string message)
        {
            StartupGateResult r;
            r.failed  = check;
            r.message = std::move(message);
            return r;
        }

        constexpr const char* kMinGpuText = "The minimum GPU is an NVIDIA GeForce RTX 2080 or equivalent.";
    } // namespace

    const char* vendorName(uint32_t vendorId)
    {
        switch (vendorId)
        {
        case kPciVendorNvidia:
            return "NVIDIA";
        case kPciVendorAmd:
            return "AMD";
        case kPciVendorIntel:
            return "Intel";
        case 0x1414:
            return "Microsoft";
        default:
            return "unknown vendor";
        }
    }

    uint32_t minimumDriverForVendor(uint32_t vendorId)
    {
        return vendorId == kPciVendorNvidia ? kMinNvidiaDriver : 0u;
    }

    uint32_t nvidiaDriverFromUmd(uint64_t umdVersion)
    {
        // NVIDIA's UMD quad is 32.0.1A.BBBB: the marketing number is the last digit of the third part
        // followed by the fourth part, so 32.0.15.7216 reads 572.16.
        const uint32_t third  = static_cast<uint32_t>((umdVersion >> 16) & 0xFFFFu);
        const uint32_t fourth = static_cast<uint32_t>(umdVersion & 0xFFFFu);
        return (third % 10u) * 10000u + fourth;
    }

    std::string formatUmdVersion(uint64_t umdVersion)
    {
        if (umdVersion == 0)
            return "unknown";
        return std::format("{}.{}.{}.{}", (umdVersion >> 48) & 0xFFFFu, (umdVersion >> 32) & 0xFFFFu, (umdVersion >> 16) & 0xFFFFu, umdVersion & 0xFFFFu);
    }

    std::string formatDriverVersion(uint32_t vendorId, uint64_t umdVersion)
    {
        if (umdVersion == 0)
            return "unknown";
        if (vendorId == kPciVendorNvidia)
        {
            const uint32_t v = nvidiaDriverFromUmd(umdVersion);
            return std::format("{}.{:02} ({})", v / 100u, v % 100u, formatUmdVersion(umdVersion));
        }
        return formatUmdVersion(umdVersion);
    }

    std::string formatFeatureLevel(uint32_t featureLevel)
    {
        if (featureLevel == 0)
            return "none";
        return std::format("{}_{}", (featureLevel >> 12) & 0xFu, (featureLevel >> 8) & 0xFu);
    }

    std::string formatShaderModel(uint32_t shaderModel)
    {
        if (shaderModel == 0)
            return "none";
        return std::format("{}.{}", (shaderModel >> 4) & 0xFu, shaderModel & 0xFu);
    }

    const char* startupGateCheckId(StartupGateCheck check)
    {
        switch (check)
        {
        case StartupGateCheck::None:
            return "none";
        case StartupGateCheck::Windows11:
            return "os";
        case StartupGateCheck::AgilityLoaded:
            return "agility";
        case StartupGateCheck::FeatureLevel:
            return "fl";
        case StartupGateCheck::ShaderModel:
            return "sm";
        case StartupGateCheck::BindingTier:
            return "binding";
        case StartupGateCheck::EnhancedBarriers:
            return "barriers";
        case StartupGateCheck::DriverVersion:
            return "driver";
        }
        return "none";
    }

    StartupGateCheck startupGateCheckFromId(const char* id)
    {
        if (!id || !*id)
            return StartupGateCheck::None;
        for (uint8_t i = 1; i <= static_cast<uint8_t>(StartupGateCheck::DriverVersion); ++i)
        {
            const StartupGateCheck check = static_cast<StartupGateCheck>(i);
            if (std::strcmp(id, startupGateCheckId(check)) == 0)
                return check;
        }
        return StartupGateCheck::None;
    }

    void applyStartupGateFake(StartupGateCheck check, StartupGateInput& in)
    {
        switch (check)
        {
        case StartupGateCheck::None:
            break;
        case StartupGateCheck::Windows11:
            in.osMajor = 10;
            in.osMinor = 0;
            in.osBuild = 19045;
            break;
        case StartupGateCheck::AgilityLoaded:
            in.agilityLoaded = false;
            in.agilityPath   = "C:\\Windows\\System32\\D3D12Core.dll";
            break;
        case StartupGateCheck::FeatureLevel:
            in.featureLevel = 0xb100;
            break;
        case StartupGateCheck::ShaderModel:
            in.shaderModel = 0x65;
            break;
        case StartupGateCheck::BindingTier:
            in.bindingTier = 2;
            break;
        case StartupGateCheck::EnhancedBarriers:
            in.enhancedBarriers = false;
            break;
        case StartupGateCheck::DriverVersion:
            // NVIDIA 531.18 (32.0.15.3118), well under any pinned minimum.
            in.vendorId        = kPciVendorNvidia;
            in.umdVersion      = (uint64_t{ 32 } << 48) | (uint64_t{ 0 } << 32) | (uint64_t{ 15 } << 16) | uint64_t{ 3118 };
            in.skipDriverCheck = false;
            break;
        }
    }

    StartupGateResult evaluateStartupGate(const StartupGateInput& in)
    {
        if (in.osBuild < kGateMinWindowsBuild)
            return fail(StartupGateCheck::Windows11,
                        std::format("DarkEngine6 needs Windows 11 (build {} or newer).\nThis PC runs Windows {}.{} build {}.", kGateMinWindowsBuild, in.osMajor, in.osMinor, in.osBuild));

        if (!in.agilityLoaded)
            return fail(StartupGateCheck::AgilityLoaded, std::format("DarkEngine6 needs the DirectX 12 Agility SDK runtime (SDK version {}) from the D3D12 folder next to the exe.\n"
                                                                     "Loaded instead: {}\n"
                                                                     "Rebuild or reinstall so D3D12\\D3D12Core.dll sits beside the exe.",
                                                                     in.agilitySdkVersion, in.agilityPath.empty() ? std::string("no D3D12Core.dll") : in.agilityPath));

        if (in.featureLevel < kGateMinFeatureLevel)
            return fail(StartupGateCheck::FeatureLevel, std::format("DarkEngine6 needs Direct3D feature level {} or higher.\n{}Found: feature level {}.\n{}", formatFeatureLevel(kGateMinFeatureLevel),
                                                                    gpuBlock(in), formatFeatureLevel(in.featureLevel), kMinGpuText));

        if (in.shaderModel < kGateMinShaderModel)
            return fail(StartupGateCheck::ShaderModel, std::format("DarkEngine6 needs Shader Model {} or higher.\n{}Found: Shader Model {}.\n{} Updating the GPU driver may help.",
                                                                   formatShaderModel(kGateMinShaderModel), gpuBlock(in), formatShaderModel(in.shaderModel), kMinGpuText));

        if (in.bindingTier < kGateMinBindingTier)
            return fail(StartupGateCheck::BindingTier,
                        std::format("DarkEngine6 needs Resource Binding Tier {}.\n{}Found: Resource Binding Tier {}.\n{}", kGateMinBindingTier, gpuBlock(in), in.bindingTier, kMinGpuText));

        if (!in.enhancedBarriers)
            return fail(StartupGateCheck::EnhancedBarriers, std::format("DarkEngine6 needs Direct3D 12 enhanced barriers, which this GPU driver does not report.\n{}"
                                                                        "Update the GPU driver. {}",
                                                                        gpuBlock(in), kMinGpuText));

        if (!in.skipDriverCheck)
        {
            const uint32_t minDriver = minimumDriverForVendor(in.vendorId);
            if (minDriver != 0)
            {
                const uint32_t found = in.vendorId == kPciVendorNvidia ? nvidiaDriverFromUmd(in.umdVersion) : 0u;
                if (in.umdVersion == 0 || found < minDriver)
                    return fail(StartupGateCheck::DriverVersion,
                                std::format("DarkEngine6 needs {} driver {}.{:02} or newer.\n{}Found: {}.\nInstall the latest driver from the GPU vendor.", vendorName(in.vendorId), minDriver / 100u,
                                            minDriver % 100u, gpuBlock(in), formatDriverVersion(in.vendorId, in.umdVersion)));
            }
        }

        return {};
    }

} // namespace Dark
