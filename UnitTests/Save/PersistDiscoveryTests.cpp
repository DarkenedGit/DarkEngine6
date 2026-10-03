#include <gtest/gtest.h>

#include "SaveTestWorld.h"

#include "ECS/World.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"

using namespace Dark;

namespace Dark
{
    struct SaveProbeComponent
    {
        static constexpr const char* kTypeName = "SaveProbe";
        static const PersistFns      kPersist;
        float kept    = 1.0f;
        float dropped = 2.0f;
    };

    struct SaveSilentComponent
    {
        static constexpr const char* kTypeName = "SaveSilent";
        int value = 7;
    };

    void captureProbe(const void* component, void* writer)
    {
        const auto& probe = *static_cast<const SaveProbeComponent*>(component);
        auto&       out   = *static_cast<Save::SaveWriter*>(writer);
        out.f32("kept", probe.kept);
        out.f32("dropped", probe.dropped);
    }

    void applyProbe(void* component, void* reader, uint16_t)
    {
        auto& probe = *static_cast<SaveProbeComponent*>(component);
        auto& in    = *static_cast<Save::SaveReader*>(reader);
        float kept    = probe.kept;
        float dropped = probe.dropped;
        if (!in.f32("kept", kept, -1.0e6f, 1.0e6f) || !in.f32("dropped", dropped, -1.0e6f, 1.0e6f))
            return;
        probe.kept    = kept;
        probe.dropped = dropped;
    }

    const PersistFns SaveProbeComponent::kPersist{ "SaveProbe", 1, 100, nullptr, captureProbe, applyProbe, nullptr };
}

namespace
{
    void bindProbeOnce()
    {
        static const bool bound = Save::bindPersist<SaveProbeComponent>();
        (void)bound;
    }
}

TEST(PersistDiscovery, OptInAppearsWithoutASaveSystemList)
{
    bindProbeOnce();
    World world;
    Entity e = world.createEntity();
    world.emplace<SaveProbeComponent>(e);
    world.emplace<SaveSilentComponent>(e);
    stampProceduralId(world, e, "unit/probe");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    EXPECT_NE(json.find("\"SaveProbe\""), std::string::npos);
    EXPECT_EQ(json.find("\"SaveSilent\""), std::string::npos);
}

TEST(PersistDiscovery, OmittedFieldStays)
{
    bindProbeOnce();
    World world;
    Entity e = world.createEntity();
    auto& probe = world.emplace<SaveProbeComponent>(e);
    probe.kept = 4.0f;
    probe.dropped = 5.0f;
    stampProceduralId(world, e, "unit/probe");
    Save::SaveSystem save;
    armSave(save, testHost());
    std::string json;
    ASSERT_EQ(save.capturePayload(world, json), Save::SaveResult::Ok);
    probe.kept = 8.0f;
    probe.dropped = 9.0f;
    nlohmann::ordered_json root;
    ASSERT_TRUE(parseSave(json, root));
    nlohmann::ordered_json* comp = nullptr;
    ASSERT_TRUE(componentJson(root, "SaveProbe", comp));
    comp->erase("dropped");
    rehashPayload(root);
    const std::string edited = root.dump(2);
    ASSERT_EQ(save.applyPayloadText(world, edited), Save::SaveResult::Ok);
    EXPECT_EQ(probe.kept, 4.0f);
    EXPECT_EQ(probe.dropped, 9.0f);
}
