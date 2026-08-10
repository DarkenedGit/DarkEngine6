#include <gtest/gtest.h>

#include "AI/Hsm.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Dark::AI;

namespace
{

// Compact event IDs for tests.
enum : HsmEventId
{
    EvGoA     = 10,
    EvGoB     = 11,
    EvGoC     = 12,
    EvGoOuter = 13,
    EvBack    = 14,
    EvShared  = 15,
    EvPing    = 16,
    EvBlock   = 17,
};

struct Trace
{
    std::vector<std::string> log;

    void push(std::string s) { log.push_back(std::move(s)); }

    HsmAction enter(const char* name)
    {
        return [this, name](HsmContext&) { push(std::string("enter:") + name); };
    }
    HsmAction exit(const char* name)
    {
        return [this, name](HsmContext&) { push(std::string("exit:") + name); };
    }
    HsmAction act(const char* name)
    {
        return [this, name](HsmContext&) { push(std::string("action:") + name); };
    }
    HsmAction handler(const char* name, bool consume)
    {
        return [this, name, consume](HsmContext& ctx) {
            push(std::string("handle:") + name);
            if (consume)
                ctx.consumed = true;
        };
    }
};

bool hasInOrder(const std::vector<std::string>& log, std::initializer_list<const char*> seq)
{
    size_t i = 0;
    for (const char* want : seq)
    {
        while (i < log.size() && log[i] != want)
            ++i;
        if (i >= log.size())
            return false;
        ++i;
    }
    return true;
}

} // namespace

TEST(Hsm, NestedEntryExitOnStartAndTransition)
{
    Trace tr;

    HsmState root{"Root"};
    HsmState a{"A"};
    HsmState b{"B"};
    a.setParent(&root).onEnter(tr.enter("A")).onExit(tr.exit("A"));
    b.setParent(&root).onEnter(tr.enter("B")).onExit(tr.exit("B"));
    root.setInitial(&a).onEnter(tr.enter("Root")).onExit(tr.exit("Root"));
    a.addTransition(EvGoB, &b, {}, tr.act("AtoB"));

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    EXPECT_TRUE(m.isIn(&root));
    EXPECT_TRUE(m.isIn(&a));
    EXPECT_EQ(m.currentLeaf(), &a);
    EXPECT_TRUE(hasInOrder(tr.log, {"enter:Root", "enter:A"}));

    tr.log.clear();
    EXPECT_TRUE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &b);
    EXPECT_TRUE(hasInOrder(tr.log, {"exit:A", "action:AtoB", "enter:B"}));
    EXPECT_FALSE(m.isIn(&a));
    EXPECT_TRUE(m.isIn(&b));
}

TEST(Hsm, DeepNestingEntryExitOrder)
{
    Trace tr;

    HsmState root{"Root"};
    HsmState mid{"Mid"};
    HsmState leaf{"Leaf"};
    HsmState other{"Other"};

    mid.setParent(&root);
    leaf.setParent(&mid);
    other.setParent(&root);
    root.setInitial(&mid).onEnter(tr.enter("Root")).onExit(tr.exit("Root"));
    mid.setInitial(&leaf).onEnter(tr.enter("Mid")).onExit(tr.exit("Mid"));
    leaf.onEnter(tr.enter("Leaf")).onExit(tr.exit("Leaf"));
    other.onEnter(tr.enter("Other")).onExit(tr.exit("Other"));

    leaf.addTransition(EvGoOuter, &other);

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    EXPECT_EQ(m.currentLeaf(), &leaf);
    EXPECT_TRUE(hasInOrder(tr.log, {"enter:Root", "enter:Mid", "enter:Leaf"}));

    tr.log.clear();
    ASSERT_TRUE(m.processEvent(HsmEvent{EvGoOuter}));
    EXPECT_EQ(m.currentLeaf(), &other);
    // Exit leaf then mid; root stays; enter other.
    EXPECT_TRUE(hasInOrder(tr.log, {"exit:Leaf", "exit:Mid", "enter:Other"}));
    EXPECT_TRUE(m.isIn(&root));
    EXPECT_FALSE(m.isIn(&mid));
}

TEST(Hsm, EnterAndExitGuards)
{
    Trace tr;
    bool allowExitA = true;
    bool allowEnterB = true;

    HsmState root{"Root"};
    HsmState a{"A"};
    HsmState b{"B"};
    a.setParent(&root).onEnter(tr.enter("A")).onExit(tr.exit("A"));
    b.setParent(&root).onEnter(tr.enter("B")).onExit(tr.exit("B"));
    root.setInitial(&a);

    a.exitGuard([&](const HsmContext&) { return allowExitA; });
    b.enterGuard([&](const HsmContext&) { return allowEnterB; });
    a.addTransition(EvGoB, &b, {}, tr.act("go"));

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());

    allowExitA = false;
    EXPECT_FALSE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &a);
    EXPECT_TRUE(tr.log.empty() || tr.log.back() != "exit:A");

    allowExitA  = true;
    allowEnterB = false;
    tr.log.clear();
    EXPECT_FALSE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &a);

    allowEnterB = true;
    tr.log.clear();
    EXPECT_TRUE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &b);
    EXPECT_TRUE(hasInOrder(tr.log, {"exit:A", "action:go", "enter:B"}));
}

TEST(Hsm, TransitionGuard)
{
    HsmState root{"Root"};
    HsmState a{"A"};
    HsmState b{"B"};
    a.setParent(&root);
    b.setParent(&root);
    root.setInitial(&a);

    bool armed = false;
    a.addTransition(
        EvGoB,
        &b,
        [&](const HsmContext&) { return armed; },
        {});

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    EXPECT_FALSE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &a);

    armed = true;
    EXPECT_TRUE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &b);
}

TEST(Hsm, ShallowHistory)
{
    Trace tr;

    HsmState root{"Root"};
    HsmState work{"Work"}; // composite with shallow history
    HsmState wA{"WA"};
    HsmState wB{"WB"};
    HsmState idle{"Idle"};

    work.setParent(&root).setHistory(HsmHistory::Shallow).onEnter(tr.enter("Work")).onExit(tr.exit("Work"));
    wA.setParent(&work).onEnter(tr.enter("WA")).onExit(tr.exit("WA"));
    wB.setParent(&work).onEnter(tr.enter("WB")).onExit(tr.exit("WB"));
    idle.setParent(&root).onEnter(tr.enter("Idle")).onExit(tr.exit("Idle"));
    work.setInitial(&wA);
    root.setInitial(&work);

    wA.addTransition(EvGoB, &wB);
    wB.addTransition(EvGoOuter, &idle);
    idle.addTransition(EvBack, &work); // re-enter Work -> shallow history should restore WB

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    EXPECT_EQ(m.currentLeaf(), &wA);

    ASSERT_TRUE(m.processEvent(HsmEvent{EvGoB}));
    EXPECT_EQ(m.currentLeaf(), &wB);

    ASSERT_TRUE(m.processEvent(HsmEvent{EvGoOuter}));
    EXPECT_EQ(m.currentLeaf(), &idle);

    tr.log.clear();
    ASSERT_TRUE(m.processEvent(HsmEvent{EvBack}));
    EXPECT_EQ(m.currentLeaf(), &wB);
    EXPECT_TRUE(hasInOrder(tr.log, {"exit:Idle", "enter:Work", "enter:WB"}));
}

TEST(Hsm, DeepHistory)
{
    HsmState root{"Root"};
    HsmState work{"Work"};
    HsmState mid{"Mid"};
    HsmState deep{"Deep"};
    HsmState other{"Other"};
    HsmState idle{"Idle"};

    work.setParent(&root).setHistory(HsmHistory::Deep);
    mid.setParent(&work);
    deep.setParent(&mid);
    other.setParent(&work);
    idle.setParent(&root);
    work.setInitial(&mid);
    mid.setInitial(&deep);
    root.setInitial(&work);

    deep.addTransition(EvGoA, &other);
    other.addTransition(EvGoOuter, &idle);
    // Return to Work with deep history should restore Mid/Deep path... but last path was Other.
    // Go: Work/Mid/Deep -> Other (still under Work) -> Idle -> back Work.
    // Deep history of Work should remember Other as the path.
    idle.addTransition(EvBack, &work);

    // From Deep go to Other first (records Work history as Other when leaving Work).
    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    EXPECT_EQ(m.currentLeaf(), &deep);

    ASSERT_TRUE(m.processEvent(HsmEvent{EvGoA}));
    EXPECT_EQ(m.currentLeaf(), &other);

    ASSERT_TRUE(m.processEvent(HsmEvent{EvGoOuter}));
    EXPECT_EQ(m.currentLeaf(), &idle);

    ASSERT_TRUE(m.processEvent(HsmEvent{EvBack}));
    EXPECT_EQ(m.currentLeaf(), &other);
    EXPECT_TRUE(m.isIn(&work));
}

TEST(Hsm, SharedTransitionsOnCompositeAndMultiSource)
{
    HsmState root{"Root"};
    HsmState region{"Region"};
    HsmState a{"A"};
    HsmState b{"B"};
    HsmState hurt{"Hurt"};

    region.setParent(&root);
    a.setParent(&region);
    b.setParent(&region);
    hurt.setParent(&root);
    region.setInitial(&a);
    root.setInitial(&region);

    // Shared among descendants: transition defined on composite.
    region.addTransition(EvShared, &hurt);

    a.addTransition(EvGoB, &b);

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    EXPECT_EQ(m.currentLeaf(), &a);

    // Event bubbles from A to Region, shared transition fires.
    ASSERT_TRUE(m.processEvent(HsmEvent{EvShared}));
    EXPECT_EQ(m.currentLeaf(), &hurt);

    // Multi-source shared via machine helper.
    HsmState x{"X"};
    HsmState y{"Y"};
    HsmState z{"Z"};
    x.setParent(&root);
    y.setParent(&root);
    z.setParent(&root);

    // Fresh machine for multi-source.
    HsmState root2{"R2"};
    x.setParent(&root2);
    y.setParent(&root2);
    z.setParent(&root2);
    root2.setInitial(&x);

    HsmMachine m2;
    m2.setRoot(&root2);
    ASSERT_TRUE(m2.addSharedTransition({&x, &y}, EvShared, &z));
    ASSERT_TRUE(m2.start());
    ASSERT_TRUE(m2.processEvent(HsmEvent{EvShared}));
    EXPECT_EQ(m2.currentLeaf(), &z);
}

TEST(Hsm, EventPropagationLeafFirst)
{
    Trace tr;

    HsmState root{"Root"};
    HsmState mid{"Mid"};
    HsmState leaf{"Leaf"};
    mid.setParent(&root);
    leaf.setParent(&mid);
    root.setInitial(&mid);
    mid.setInitial(&leaf);

    leaf.onEvent(tr.handler("Leaf", false));
    mid.onEvent(tr.handler("Mid", false));
    root.onEvent(tr.handler("Root", true)); // consumes

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    tr.log.clear();

    EXPECT_TRUE(m.processEvent(HsmEvent{EvPing}));
    ASSERT_EQ(tr.log.size(), 3u);
    EXPECT_EQ(tr.log[0], "handle:Leaf");
    EXPECT_EQ(tr.log[1], "handle:Mid");
    EXPECT_EQ(tr.log[2], "handle:Root");

    // Consume at leaf: parents not called.
    leaf.onEvent(tr.handler("Leaf2", true));
    tr.log.clear();
    // Note: both Leaf handlers run (order registered); second consumes — Mid/Root still skipped after consume.
    // Actually first Leaf handler does not consume, second does — then stop before Mid.
    // We added a second handler; collectHandlers walks state handlers then base.
    EXPECT_TRUE(m.processEvent(HsmEvent{EvPing}));
    EXPECT_EQ(tr.log[0], "handle:Leaf");
    EXPECT_EQ(tr.log[1], "handle:Leaf2");
    EXPECT_TRUE(std::find(tr.log.begin(), tr.log.end(), "handle:Mid") == tr.log.end());
}

TEST(Hsm, BehaviorInheritanceTransitionsAndHandlers)
{
    Trace tr;

    HsmState base{"Base"};
    HsmState derived{"Derived"};
    HsmState target{"Target"};
    HsmState root{"Root"};

    derived.setParent(&root).setBase(&base);
    target.setParent(&root);
    root.setInitial(&derived);

    base.onEnter(tr.enter("Base")).onExit(tr.exit("Base"));
    derived.onEnter(tr.enter("Derived")).onExit(tr.exit("Derived"));
    target.onEnter(tr.enter("Target"));

    // Transition only on base — inherited by derived.
    base.addTransition(EvGoA, &target, {}, tr.act("baseGo"));
    base.onEvent(tr.handler("BaseH", false));
    derived.onEvent(tr.handler("DerivedH", false));

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    // Enter: base then derived (inherit entry).
    EXPECT_TRUE(hasInOrder(tr.log, {"enter:Base", "enter:Derived"}));

    tr.log.clear();
    // Handlers run during bubble; without consume/transition processEvent returns false.
    (void)m.processEvent(HsmEvent{EvPing});
    // Handlers: derived first, then base.
    ASSERT_GE(tr.log.size(), 2u);
    EXPECT_EQ(tr.log[0], "handle:DerivedH");
    EXPECT_EQ(tr.log[1], "handle:BaseH");

    tr.log.clear();
    ASSERT_TRUE(m.processEvent(HsmEvent{EvGoA}));
    EXPECT_EQ(m.currentLeaf(), &target);
    EXPECT_TRUE(hasInOrder(tr.log, {"exit:Derived", "exit:Base", "action:baseGo", "enter:Target"}));
}

TEST(Hsm, InternalTransitionNoStateChange)
{
    Trace tr;
    int count = 0;

    HsmState root{"Root"};
    HsmState a{"A"};
    a.setParent(&root).onEnter(tr.enter("A")).onExit(tr.exit("A"));
    root.setInitial(&a);
    a.addInternalTransition(EvPing, {}, [&](HsmContext&) {
        ++count;
        tr.push("internal");
    });

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    tr.log.clear();

    ASSERT_TRUE(m.processEvent(HsmEvent{EvPing}));
    EXPECT_EQ(m.currentLeaf(), &a);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(tr.log.size(), 1u);
    EXPECT_EQ(tr.log[0], "internal");
    // No exit/enter of A.
    EXPECT_TRUE(std::find(tr.log.begin(), tr.log.end(), "exit:A") == tr.log.end());
}

TEST(Hsm, OwnerPointerOnContext)
{
    int owner = 42;
    HsmState root{"Root"};
    HsmState a{"A"};
    a.setParent(&root);
    root.setInitial(&a);

    void* seen = nullptr;
    a.onEvent([&](HsmContext& ctx) {
        seen = ctx.owner();
        ctx.consumed = true;
    });

    HsmMachine m;
    m.setRoot(&root);
    m.setOwner(&owner);
    ASSERT_TRUE(m.start());
    ASSERT_TRUE(m.processEvent(HsmEvent{EvPing}));
    EXPECT_EQ(seen, &owner);
}

TEST(Hsm, StopExitsActiveConfiguration)
{
    Trace tr;
    HsmState root{"Root"};
    HsmState a{"A"};
    a.setParent(&root).onEnter(tr.enter("A")).onExit(tr.exit("A"));
    root.setInitial(&a).onEnter(tr.enter("Root")).onExit(tr.exit("Root"));

    HsmMachine m;
    m.setRoot(&root);
    ASSERT_TRUE(m.start());
    tr.log.clear();
    m.stop();
    EXPECT_FALSE(m.isRunning());
    EXPECT_TRUE(hasInOrder(tr.log, {"exit:A", "exit:Root"}));
}
