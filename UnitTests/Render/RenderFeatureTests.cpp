#include <gtest/gtest.h>

#include "Core/Log.h"
#include "Render/FrameFeatures.h"
#include "Render/HdrDrawInputs.h"
#include "Render/RenderFeature.h"
#include "RenderGraphTestUtil.h"

#include <cstdint>
#include <string>
#include <vector>

// RenderFeatureRegistry: features kept in stage then registration order, rejected duplicates,
// create failures skipped, and created features declared on the host's feature list.

using namespace Dark;

namespace
{
    constexpr uint32_t kBgra8 = 87;
    constexpr uint32_t kD32   = 40;
    constexpr uint32_t kRgba  = 10;

    std::vector<std::string> g_errors;
    void                     captureErrors(LogLevel level, LogCategory, const char* message)
    {
        if (level == LogLevel::Error)
            g_errors.emplace_back(message);
    }
    struct ErrorCapture
    {
        ErrorCapture()
        {
            g_errors.clear();
            Log::addCapture(&captureErrors);
        }
        ~ErrorCapture()
        {
            Log::removeCapture(&captureErrors);
        }
    };

    struct FakeFeature final : IRenderFeature
    {
        FakeFeature(const char* n, FeatureStage s, PassId p, std::vector<std::string>* log, bool ok = true) :
            featureName(n),
            featureStage(s),
            id(p),
            events(log),
            createOk(ok)
        {
        }

        const char* name() const override
        {
            return featureName;
        }
        FeatureStage stage() const override
        {
            return featureStage;
        }
        PassId pass() const override
        {
            return id;
        }
        bool create(const FeatureCreateInfo& info) override
        {
            ++creates;
            lastInfo = info;
            events->push_back(std::string("create ") + featureName);
            return createOk;
        }
        void destroy() override
        {
            events->push_back(std::string("destroy ") + featureName);
        }
        void addPasses(FeatureBuilder& b, const FrameView& view) override
        {
            ++declares;
            heldTemporal = view.holdTemporal;
            if (id == PassId::Sky)
                added = addHdrDrawPass(b, featureName, skyUses(),
                                       [this](Rg::PassContext&, const HdrDrawTargets& d)
                                       {
                                           events->push_back(std::string("run ") + featureName);
                                           targets = d;
                                       });
            else
                b.opaque(featureName, {}, {}, [this](Rg::PassContext&) { events->push_back(std::string("run ") + featureName); });
        }

        const char*               featureName;
        FeatureStage              featureStage;
        PassId                    id;
        std::vector<std::string>* events;
        bool                      createOk;
        int                       creates      = 0;
        int                       declares     = 0;
        bool                      heldTemporal = false;
        bool                      added        = false;
        HdrDrawTargets            targets{};
        FeatureCreateInfo         lastInfo{};
    };

    std::vector<std::string> names(const RenderFeatureRegistry& r)
    {
        std::vector<std::string> out;
        for (uint32_t i = 0; i < r.count(); ++i)
            out.emplace_back(r.feature(i)->name());
        return out;
    }

    ID3D12Resource* fakeResource(uint32_t r)
    {
        return reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x100 + r * 0x10));
    }

    Rg::TextureDesc absDesc(uint32_t w, uint32_t h, uint32_t format)
    {
        Rg::TextureDesc d;
        d.sizeMode = Rg::SizeMode::Absolute;
        d.width    = w;
        d.height   = h;
        d.format   = format;
        return d;
    }

    // A Sandbox frame: every list entry the registry does not declare is an opaque pass that logs itself.
    struct SandboxFrameRun
    {
        Rg::RenderGraph           graph;
        FeatureFrame              features;
        std::vector<std::string>* events   = nullptr;
        uint32_t                  declared = 0;

        bool run(const RenderFeatureRegistry& registry, const FrameView& view)
        {
            uint32_t           count = 0;
            const FeaturePass* list  = sandboxFeatures(count);
            graph.beginFrame(1280, 720);
            features.begin(list, count);
            for (uint32_t r = 0; r < kRenderResourceCount; ++r)
            {
                const auto     res    = static_cast<RenderResource>(r);
                const uint32_t fmt    = res == RenderResource::Swap ? kBgra8 : (res == RenderResource::Depth ? kD32 : kRgba);
                Rg::Layout     layout = Rg::Layout::ShaderResource;
                if (res == RenderResource::Swap || res == RenderResource::Hdr)
                    layout = Rg::Layout::RenderTarget;
                features.importResource(graph, res, fakeResource(r), absDesc(1280, 720, fmt), layout);
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                const PassId id = list[i].id;
                features.set(id, [this, id](Rg::PassContext&) { events->push_back(std::string("run ") + passName(id)); });
            }
            declared = registry.declare(features, view);
            if (!features.compileFrame(graph, "test"))
                return false;
            return graph.execute();
        }
    };

    int indexOf(const std::vector<std::string>& v, const std::string& s)
    {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == s)
                return static_cast<int>(i);
        return -1;
    }
} // namespace

TEST(RenderFeature, RegistryOrdersByStageThenRegistration)
{
    std::vector<std::string> events;
    FakeFeature              post("PostA", FeatureStage::Post, PassId::Post, &events);
    FakeFeature              skyA("SkyA", FeatureStage::Sky, PassId::Sky, &events);
    FakeFeature              water("Water", FeatureStage::Transparent, PassId::Water, &events);
    FakeFeature              skyB("SkyB", FeatureStage::Sky, PassId::Clouds, &events);
    FakeFeature              shadows("Shadows", FeatureStage::Shadows, PassId::Shadows, &events);
    FakeFeature              ui("Ui", FeatureStage::Ui, PassId::Overlay, &events);
    RenderFeatureRegistry    r;
    for (IRenderFeature* f : std::initializer_list<IRenderFeature*>{ &post, &skyA, &water, &skyB, &shadows, &ui })
        ASSERT_TRUE(r.add(f));
    EXPECT_EQ(names(r), (std::vector<std::string>{ "Shadows", "SkyA", "SkyB", "Water", "PostA", "Ui" }));
    EXPECT_EQ(r.find(PassId::Clouds), &skyB);
    EXPECT_EQ(r.find(PassId::GBuffer), nullptr);
}

TEST(RenderFeature, RegistryRejectsNullDuplicatesAndASecondFeatureOnOnePass)
{
    ErrorCapture             errors;
    std::vector<std::string> events;
    FakeFeature              sky("Sky", FeatureStage::Sky, PassId::Sky, &events);
    FakeFeature              other("OtherSky", FeatureStage::Transparent, PassId::Sky, &events);
    RenderFeatureRegistry    r;
    EXPECT_FALSE(r.add(nullptr));
    EXPECT_TRUE(r.add(&sky));
    EXPECT_FALSE(r.add(&sky));
    EXPECT_FALSE(r.add(&other));
    EXPECT_EQ(names(r), (std::vector<std::string>{ "Sky" }));
    ASSERT_EQ(g_errors.size(), 3u);
    EXPECT_NE(g_errors[0].find("null feature"), std::string::npos) << g_errors[0];
    EXPECT_NE(g_errors[1].find("Sky is already registered"), std::string::npos) << g_errors[1];
    EXPECT_NE(g_errors[2].find("Sky and OtherSky both declare Sky"), std::string::npos) << g_errors[2];
}

TEST(RenderFeature, CreateFailureIsLoggedAndOnlyThatFeatureIsSkipped)
{
    ErrorCapture             errors;
    std::vector<std::string> events;
    FakeFeature              sky("Sky", FeatureStage::Sky, PassId::Sky, &events, false);
    FakeFeature              water("Water", FeatureStage::Transparent, PassId::Water, &events);
    RenderFeatureRegistry    r;
    ASSERT_TRUE(r.add(&water));
    ASSERT_TRUE(r.add(&sky));
    FeatureCreateInfo info;
    info.logTag = "Host";
    EXPECT_FALSE(r.createAll(info));
    EXPECT_FALSE(r.isCreated(0)); // Sky
    EXPECT_TRUE(r.isCreated(1));  // Water
    EXPECT_STREQ(water.lastInfo.logTag, "Host");
    ASSERT_EQ(g_errors.size(), 1u);
    EXPECT_NE(g_errors[0].find("Host: Sky feature create failed"), std::string::npos) << g_errors[0];

    SandboxFrameRun frame;
    frame.events = &events;
    events.clear();
    ASSERT_TRUE(frame.run(r, FrameView{}));
    EXPECT_EQ(frame.declared, 1u);
    EXPECT_EQ(sky.declares, 0);
    EXPECT_EQ(water.declares, 1);
    // The skipped entry keeps the host's opaque callback; the created one replaces it.
    EXPECT_GE(indexOf(events, "run Sky"), 0);
    EXPECT_GE(indexOf(events, "run Water"), 0);

    // A second createAll retries only what failed.
    sky.createOk = true;
    EXPECT_TRUE(r.createAll(info));
    EXPECT_EQ(sky.creates, 2);
    EXPECT_EQ(water.creates, 1);
}

TEST(RenderFeature, DestroyAllDestroysCreatedFeaturesInReverseOnce)
{
    std::vector<std::string> events;
    FakeFeature              a("A", FeatureStage::Sky, PassId::Sky, &events);
    FakeFeature              b("B", FeatureStage::Transparent, PassId::Water, &events, false);
    FakeFeature              c("C", FeatureStage::Post, PassId::Post, &events);
    RenderFeatureRegistry    r;
    r.add(&c);
    r.add(&a);
    r.add(&b);
    (void)r.createAll({});
    events.clear();
    r.destroyAll();
    EXPECT_EQ(events, (std::vector<std::string>{ "destroy C", "destroy A" }));
    events.clear();
    r.destroyAll();
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(r.count(), 3u);

    (void)r.createAll({});
    events.clear();
    r.clear();
    EXPECT_EQ(events, (std::vector<std::string>{ "destroy C", "destroy A" }));
    EXPECT_EQ(r.count(), 0u);
}

TEST(RenderFeature, UncreatedFeaturesAreNotDeclared)
{
    std::vector<std::string> events;
    FakeFeature              sky("Sky", FeatureStage::Sky, PassId::Sky, &events);
    RenderFeatureRegistry    r;
    r.add(&sky);
    SandboxFrameRun frame;
    frame.events = &events;
    ASSERT_TRUE(frame.run(r, FrameView{}));
    EXPECT_EQ(frame.declared, 0u);
    EXPECT_EQ(sky.declares, 0);
}

TEST(RenderFeature, DeclaredFeatureDrawsInItsListSlotWithTheFrameView)
{
    std::vector<std::string> events;
    FakeFeature              sky("SkyFeature", FeatureStage::Sky, PassId::Sky, &events);
    RenderFeatureRegistry    r;
    r.add(&sky);
    ASSERT_TRUE(r.createAll({}));

    SandboxFrameRun frame;
    frame.events = &events;
    FrameView view;
    view.holdTemporal = true;
    ASSERT_TRUE(frame.run(r, view));
    EXPECT_TRUE(frame.features.orderOk());
    EXPECT_EQ(frame.declared, 1u);
    EXPECT_EQ(sky.declares, 1);
    EXPECT_TRUE(sky.heldTemporal);
    ASSERT_TRUE(sky.added);
    EXPECT_TRUE(sky.targets.hdr.valid());
    EXPECT_TRUE(sky.targets.depth.valid());

    const int skyAt = indexOf(events, "run SkyFeature");
    ASSERT_GE(skyAt, 0);
    EXPECT_EQ(indexOf(events, "run Sky"), -1); // the feature replaced the host's callback
    const int localAt = indexOf(events, std::string("run ") + passName(PassId::LocalVolumes));
    ASSERT_GE(localAt, 0);
    EXPECT_LT(localAt, skyAt);
    EXPECT_LT(skyAt, indexOf(events, std::string("run ") + passName(PassId::SsrCapture)));
}

TEST(RenderFeature, AFeatureOnAPassOutsideTheHostListIsNotDeclared)
{
    ErrorCapture             errors;
    std::vector<std::string> events;
    FakeFeature              grid("Grid", FeatureStage::Overlay, PassId::EditorGrid, &events);
    FakeFeature              sky("Sky", FeatureStage::Sky, PassId::Sky, &events);
    RenderFeatureRegistry    r;
    r.add(&grid);
    r.add(&sky);
    ASSERT_TRUE(r.createAll({}));
    SandboxFrameRun frame;
    frame.events = &events;
    ASSERT_TRUE(frame.run(r, FrameView{}));
    EXPECT_EQ(frame.declared, 1u);
    EXPECT_EQ(grid.declares, 0);
    EXPECT_EQ(sky.declares, 1);
}
