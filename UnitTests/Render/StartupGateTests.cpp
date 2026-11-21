#include <gtest/gtest.h>

#include "Render/StartupGate.h"

#include <string>

using namespace Dark;

namespace
{

    // A machine that passes: Windows 11 24H2, Agility loaded, RTX 2080 on a recent driver.
    StartupGateInput passingInput()
    {
        StartupGateInput in;
        in.osMajor           = 10;
        in.osMinor           = 0;
        in.osBuild           = 26100;
        in.agilityLoaded     = true;
        in.agilityPath       = "C:\\DE6\\bin\\Debug\\D3D12\\D3D12Core.dll";
        in.agilitySdkVersion = 620;
        in.adapterName       = "NVIDIA GeForce RTX 2080";
        in.vendorId          = kPciVendorNvidia;
        in.featureLevel      = 0xc200;
        in.shaderModel       = 0x68;
        in.bindingTier       = 3;
        in.enhancedBarriers  = true;
        // 32.0.15.8180 -> 581.80
        in.umdVersion = (uint64_t{ 32 } << 48) | (uint64_t{ 15 } << 16) | uint64_t{ 8180 };
        return in;
    }

    bool contains(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }

} // namespace

TEST(StartupGate, PassingMachinePasses)
{
    const StartupGateResult r = evaluateStartupGate(passingInput());
    EXPECT_TRUE(r.passed());
    EXPECT_TRUE(r.message.empty());
}

TEST(StartupGate, NvidiaDriverNumberFromUmd)
{
    EXPECT_EQ(nvidiaDriverFromUmd((uint64_t{ 32 } << 48) | (uint64_t{ 15 } << 16) | uint64_t{ 7216 }), 57216u);
    EXPECT_EQ(nvidiaDriverFromUmd((uint64_t{ 31 } << 48) | (uint64_t{ 14 } << 16) | uint64_t{ 6014 }), 46014u);
    EXPECT_EQ(formatUmdVersion((uint64_t{ 32 } << 48) | (uint64_t{ 15 } << 16) | uint64_t{ 7216 }), "32.0.15.7216");
    EXPECT_EQ(formatDriverVersion(kPciVendorNvidia, (uint64_t{ 32 } << 48) | (uint64_t{ 15 } << 16) | uint64_t{ 7216 }), "572.16 (32.0.15.7216)");
    EXPECT_EQ(formatDriverVersion(kPciVendorAmd, (uint64_t{ 31 } << 48) | (uint64_t{ 24 } << 16) | uint64_t{ 1001 }), "31.0.24.1001");
    EXPECT_EQ(formatDriverVersion(kPciVendorNvidia, 0), "unknown");
}

TEST(StartupGate, Formatting)
{
    EXPECT_EQ(formatFeatureLevel(0xc100), "12_1");
    EXPECT_EQ(formatFeatureLevel(0xb000), "11_0");
    EXPECT_EQ(formatShaderModel(0x66), "6.6");
    EXPECT_EQ(formatShaderModel(0x51), "5.1");
}

TEST(StartupGate, CheckIdsRoundTrip)
{
    for (uint8_t i = 1; i <= static_cast<uint8_t>(StartupGateCheck::DriverVersion); ++i)
    {
        const StartupGateCheck check = static_cast<StartupGateCheck>(i);
        EXPECT_EQ(startupGateCheckFromId(startupGateCheckId(check)), check);
    }
    EXPECT_EQ(startupGateCheckFromId("nope"), StartupGateCheck::None);
    EXPECT_EQ(startupGateCheckFromId(""), StartupGateCheck::None);
    EXPECT_EQ(startupGateCheckFromId(nullptr), StartupGateCheck::None);
}

// Each fake fails exactly its own check on a passing machine, with the plan's message content
// (GPU, found value, minimum, reason).
TEST(StartupGate, EachFakeFailsItsCheckWithAClearMessage)
{
    struct Case
    {
        StartupGateCheck check;
        const char*      mustContain[3];
    };
    const Case cases[] = {
        { StartupGateCheck::Windows11, { "Windows 11", "build 22000", "build 19045" } },
        { StartupGateCheck::AgilityLoaded, { "Agility SDK", "620", "System32" } },
        { StartupGateCheck::FeatureLevel, { "feature level 12_1", "Found: feature level 11_1", "RTX 2080" } },
        { StartupGateCheck::ShaderModel, { "Shader Model 6.6", "Found: Shader Model 6.5", "NVIDIA GeForce RTX 2080" } },
        { StartupGateCheck::BindingTier, { "Resource Binding Tier 3", "Found: Resource Binding Tier 2", "RTX 2080" } },
        { StartupGateCheck::EnhancedBarriers, { "enhanced barriers", "Update the GPU driver", "Driver: 581.80" } },
        { StartupGateCheck::DriverVersion, { "NVIDIA driver 572.16 or newer", "Found: 531.18", "GPU: NVIDIA GeForce RTX 2080" } },
    };
    for (const Case& c : cases)
    {
        StartupGateInput in = passingInput();
        applyStartupGateFake(c.check, in);
        const StartupGateResult r = evaluateStartupGate(in);
        EXPECT_EQ(r.failed, c.check) << startupGateCheckId(c.check);
        for (const char* part : c.mustContain)
            EXPECT_TRUE(contains(r.message, part)) << startupGateCheckId(c.check) << ": missing '" << part << "' in:\n" << r.message;
    }
}

TEST(StartupGate, OrderReportsFirstFailure)
{
    StartupGateInput in = passingInput();
    in.osBuild          = 19045;
    in.featureLevel     = 0xb000;
    in.enhancedBarriers = false;
    EXPECT_EQ(evaluateStartupGate(in).failed, StartupGateCheck::Windows11);
    in.osBuild = 22000;
    EXPECT_EQ(evaluateStartupGate(in).failed, StartupGateCheck::FeatureLevel);
    in.featureLevel = 0xc100;
    EXPECT_EQ(evaluateStartupGate(in).failed, StartupGateCheck::EnhancedBarriers);
}

TEST(StartupGate, DriverMinimumIsInclusiveAndPerVendor)
{
    StartupGateInput in = passingInput();
    in.umdVersion       = (uint64_t{ 32 } << 48) | (uint64_t{ 15 } << 16) | uint64_t{ 7216 }; // exactly 572.16
    EXPECT_TRUE(evaluateStartupGate(in).passed());
    in.umdVersion = (uint64_t{ 32 } << 48) | (uint64_t{ 15 } << 16) | uint64_t{ 7215 };
    EXPECT_EQ(evaluateStartupGate(in).failed, StartupGateCheck::DriverVersion);
    in.umdVersion = 0;
    EXPECT_EQ(evaluateStartupGate(in).failed, StartupGateCheck::DriverVersion);

    // No pinned minimum for other vendors yet: the caps checks still apply.
    in.vendorId   = kPciVendorAmd;
    in.umdVersion = (uint64_t{ 31 } << 48) | (uint64_t{ 24 } << 16) | uint64_t{ 1001 };
    EXPECT_TRUE(evaluateStartupGate(in).passed());
    EXPECT_EQ(minimumDriverForVendor(kPciVendorAmd), 0u);
    EXPECT_EQ(minimumDriverForVendor(kPciVendorNvidia), kMinNvidiaDriver);
}

TEST(StartupGate, WarpSkipsOnlyTheDriverCheck)
{
    StartupGateInput in = passingInput();
    in.skipDriverCheck  = true;
    in.umdVersion       = 0;
    EXPECT_TRUE(evaluateStartupGate(in).passed());
    in.enhancedBarriers = false;
    EXPECT_EQ(evaluateStartupGate(in).failed, StartupGateCheck::EnhancedBarriers);
}
