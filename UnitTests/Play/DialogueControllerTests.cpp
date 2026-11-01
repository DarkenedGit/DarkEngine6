#include "Assets/AssetManager.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/Carry.h"
#include "Play/DialogueBinding.h"
#include "Play/DialogueController.h"
#include "Play/DialogueFacts.h"
#include "Play/DialogueMemory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace Dark;

namespace
{
    const char* kGraph = R"({
        "version": 1,
        "id": "gate",
        "pauseWorld": false,
        "talkRange": 6,
        "leash": 8,
        "speaker": "Guard",
        "entry": "hello",
        "refusal": [ { "id": "no", "text": "Not now.", "once": false } ],
        "resume": [ { "id": "back", "text": "As I was saying.", "once": false } ],
        "nodes": [
            {
                "id": "hello",
                "lines": [ { "id": "hi", "text": "Hold there.", "once": false } ],
                "choices": [ { "id": "ask", "text": "Go on", "next": "hello", "once": false } ]
            }
        ]
    })";

    struct Mount
    {
        std::filesystem::path dir;
        AssetManager          assets;

        Mount()
        {
            dir = std::filesystem::temp_directory_path() / "dark-dialogue-controller";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            const std::filesystem::path file = dir / "talk.dlg.json";
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << kGraph;
            assets.mountDirectory(dir);
        }

        ~Mount()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    };

    struct View
    {
        World*         world  = nullptr;
        AssetManager*  assets = nullptr;
        Entity         player{};
        Entity         session{};
        Math::Vector3f look{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f cameraPos{ 0.0f, 1.6f, 0.0f };
        bool           enabled = true;
    };

    void fillView(void* user, DialogueHost& host)
    {
        const View* view = static_cast<const View*>(user);
        host.world      = view->world;
        host.assets     = view->assets;
        host.player     = view->player;
        host.session    = view->session;
        host.look       = view->look;
        host.cameraPos  = view->cameraPos;
        host.enabled    = view->enabled;
    }

    Entity addNpc(World& world, float x, float z, const char* graph)
    {
        const Entity npc = world.createEntity();
        TransformComponent& xf = world.emplace<TransformComponent>(npc);
        xf.position = Math::Vector3f{ x, 0.0f, z };
        DialogueBindingComponent& binding = world.emplace<DialogueBindingComponent>(npc);
        binding.graph = graph;
        return npc;
    }
}

TEST(DialogueController, AimPicksTheNpcInFront)
{
    World world;
    const Entity player = world.createEntity();
    TransformComponent& playerXf = world.emplace<TransformComponent>(player);
    playerXf.position = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
    DialogueBindingComponent& self = world.emplace<DialogueBindingComponent>(player);
    self.graph = "self.dlg.json";
    addNpc(world, 0.0f, 1.0f, "");
    const Entity front = addNpc(world, 0.0f, 2.0f, "talk.dlg.json");
    const Entity side = addNpc(world, 2.0f, 0.0f, "talk.dlg.json");

    View view;
    view.world = &world;
    view.player = player;
    DialogueController talk;
    talk.bind(&fillView, &view);

    EXPECT_EQ(talk.pickTarget(), front);

    view.look = Math::Vector3f{ 1.0f, 0.0f, 0.0f };
    EXPECT_EQ(talk.pickTarget(), side);

    view.look = Math::Vector3f{ 0.0f, 0.0f, 1.0f };
    world.get<TransformComponent>(front)->position.z = 4.0f;
    EXPECT_FALSE(talk.pickTarget().valid());

    world.get<TransformComponent>(front)->position.z = 2.0f;
    view.enabled = false;
    EXPECT_FALSE(talk.pickTarget().valid());
    view.enabled = true;

    const Entity carried = world.createEntity();
    world.emplace<CarriedByComponent>(carried);
    EXPECT_FALSE(talk.pickTarget().valid());
    world.remove<CarriedByComponent>(carried);
    EXPECT_EQ(talk.pickTarget(), front);
}

TEST(DialogueController, PickUsesGraphTalkRange)
{
    Mount mount;
    ASSERT_TRUE(mount.assets.tryLoadDialogueGraph("talk.dlg.json"));

    World world;
    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player);
    const Entity near = addNpc(world, 0.0f, 4.0f, "talk.dlg.json");
    const Entity far = addNpc(world, 0.0f, 7.0f, "talk.dlg.json");

    View view;
    view.world = &world;
    view.assets = &mount.assets;
    view.player = player;
    DialogueController talk;
    talk.bind(&fillView, &view);

    EXPECT_EQ(talk.pickTarget(), near);
    world.remove<DialogueBindingComponent>(near);
    EXPECT_FALSE(talk.pickTarget().valid());
    EXPECT_NE(talk.pickTarget(), far);
}

TEST(DialogueController, InterruptKeepsTheBookmark)
{
    Mount mount;
    World world;
    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player);
    const Entity npc = addNpc(world, 0.0f, 2.0f, "talk.dlg.json");

    View view;
    view.world = &world;
    view.assets = &mount.assets;
    view.player = player;
    view.session = player;
    DialogueController talk;
    talk.bind(&fillView, &view);

    talk.tryBegin();
    EXPECT_EQ(talk.runtime().phase(), DialoguePhase::NodeLine);
    EXPECT_EQ(talk.runtime().nodeId(), "hello");
    talk.interrupt(DialogueInterrupt::Cancel);
    EXPECT_FALSE(talk.active());

    DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(npc);
    ASSERT_NE(memory, nullptr);
    EXPECT_TRUE(memory->bookmark.valid);
    EXPECT_EQ(memory->bookmark.nodeId, "hello");

    talk.tryBegin();
    EXPECT_EQ(talk.runtime().phase(), DialoguePhase::Resume);
}

TEST(DialogueController, EndPlayStartsTheNextTalkAtTheGreeting)
{
    Mount mount;
    World world;
    const Entity player = world.createEntity();
    world.emplace<TransformComponent>(player);
    DialogueFactsComponent& facts = world.emplace<DialogueFactsComponent>(player);
    facts.set("met", 3);
    const Entity npc = addNpc(world, 0.0f, 2.0f, "talk.dlg.json");

    View view;
    view.world = &world;
    view.assets = &mount.assets;
    view.player = player;
    view.session = player;
    DialogueController talk;
    talk.bind(&fillView, &view);

    talk.tryBegin();
    ASSERT_TRUE(talk.active());
    EXPECT_EQ(talk.runtime().phase(), DialoguePhase::NodeLine);

    DialogueMemoryComponent* memory = world.get<DialogueMemoryComponent>(npc);
    ASSERT_NE(memory, nullptr);
    memory->lines.onceHeard.push_back("hi");
    EXPECT_TRUE(memory->bookmark.valid);

    talk.endPlay();
    EXPECT_FALSE(talk.active());
    EXPECT_FALSE(memory->bookmark.valid);
    EXPECT_TRUE(memory->lines.onceHeard.empty());
    EXPECT_TRUE(facts.facts.empty());
    EXPECT_FALSE(talk.runtime().graph());

    talk.tryBegin();
    EXPECT_EQ(talk.runtime().phase(), DialoguePhase::NodeLine);
    EXPECT_EQ(talk.runtime().nodeId(), "hello");

    const Entity bare = world.createEntity();
    view.session = bare;
    talk.endPlay();
    EXPECT_EQ(world.get<DialogueFactsComponent>(bare), nullptr);
}
