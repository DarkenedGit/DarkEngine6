#include <gtest/gtest.h>

#include "Combat/DamageEvent.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusEffectComponent.h"
#include "Combat/StatusFxEvent.h"
#include "Combat/StatusId.h"
#include "ECS/Entity.h"

using namespace Dark;
using namespace Dark::Combat;

namespace
{
    bool drainHasOp(StatusEffectComponent& st, StatusFxOp op, uint8_t statusId)
    {
        StatusFxEvent evs[StatusEffectComponent::kMaxFxEvents]{};
        const int     n = drainStatusFx(st, evs, StatusEffectComponent::kMaxFxEvents);
        for (int i = 0; i < n; ++i)
        {
            if (evs[i].op == op && evs[i].statusId == statusId)
                return true;
        }
        return false;
    }
} // namespace

TEST(Status_InstanceAppendOnly, FiveArgAggregateKeepsDefaults)
{
    StatusInstance inst{ 1, 1.0f, 2.0f, true, CcCategory::Stun };
    EXPECT_EQ(inst.id, 1);
    EXPECT_FLOAT_EQ(inst.magnitude, 1.0f);
    EXPECT_FLOAT_EQ(inst.remaining, 2.0f);
    EXPECT_TRUE(inst.hard);
    EXPECT_EQ(inst.category, CcCategory::Stun);
    EXPECT_EQ(inst.stacks, 1);
    EXPECT_FLOAT_EQ(inst.tickAcc, 0.0f);
    EXPECT_FALSE(inst.source.valid());
}

TEST(Status_ApplyPoison_HasAndRemaining, SlotQueries)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 8.0f, 1.0e-4f);
    EXPECT_TRUE(st.has(StatusId::Poison));
    EXPECT_NEAR(st.remaining(StatusId::Poison), 8.0f, 1.0e-4f);
    EXPECT_EQ(st.stacks(StatusId::Poison), 1);
    EXPECT_FALSE(st.hasHardCc());
    EXPECT_FALSE(st.has(StatusId::Bleed));
    EXPECT_EQ(st.stacks(StatusId::Bleed), 0);
    EXPECT_FLOAT_EQ(st.remaining(StatusId::Bleed), 0.0f);
}

TEST(Status_PoisonRefresh_DurationReplaced, RemainingAlwaysDurMaxMag)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    st.slots[0].remaining = 5.0f;
    EXPECT_NEAR(st.applyStatus(StatusId::Poison, 3.0f, 2.0f), 3.0f, 1.0e-4f);
    EXPECT_NEAR(st.remaining(StatusId::Poison), 3.0f, 1.0e-4f);
    EXPECT_NEAR(st.slots[0].magnitude, 2.0f, 1.0e-4f);
    EXPECT_EQ(st.stacks(StatusId::Poison), 1);
    EXPECT_TRUE(drainHasOp(st, StatusFxOp::Refreshed, static_cast<uint8_t>(StatusId::Poison)));
}

TEST(Status_BleedRefresh_NoStacks, TwoAppliesStayAtOne)
{
    StatusEffectComponent st{};
    const StatusDef*      bleed = statusDef(StatusId::Bleed);
    ASSERT_NE(bleed, nullptr);
    EXPECT_NEAR(st.applyStatus(StatusId::Bleed, 0.0f, 0.0f), bleed->defaultDuration, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Bleed, 0.0f, 0.0f), bleed->defaultDuration, 1.0e-4f);
    EXPECT_EQ(st.stacks(StatusId::Bleed), 1);
    EXPECT_NEAR(st.remaining(StatusId::Bleed), 6.0f, 1.0e-4f);
    EXPECT_EQ(st.count, 1);
}

TEST(Status_Chill_MoveSpeedScale, DefaultHalfClampBounds)
{
    StatusEffectComponent st{};
    EXPECT_FLOAT_EQ(st.moveSpeedScale(), 1.0f);

    EXPECT_NEAR(st.applyStatus(StatusId::Chill, 0.0f, 0.0f), 6.0f, 1.0e-4f);
    EXPECT_NEAR(st.remaining(StatusId::Chill), 6.0f, 1.0e-4f);
    EXPECT_NEAR(st.moveSpeedScale(), 0.5f, 1.0e-4f);
    EXPECT_FALSE(st.hasHardCc());
    EXPECT_FALSE(st.hasCategory(CcCategory::Root));

    st.reset();
    ASSERT_GT(st.applyStatus(StatusId::Chill, 6.0f, 0.1f), 0.0f);
    EXPECT_NEAR(st.moveSpeedScale(), 0.25f, 1.0e-4f);

    st.reset();
    ASSERT_GT(st.applyStatus(StatusId::Chill, 6.0f, 2.0f), 0.0f);
    EXPECT_NEAR(st.moveSpeedScale(), 1.0f, 1.0e-4f);
}

TEST(Status_Shock_DoesNotConsumeStunDr, ThreeStunsStill2_1_0)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyStatus(StatusId::Shock, 0.0f, 0.0f), 3.0f, 1.0e-4f);
    EXPECT_TRUE(st.has(StatusId::Shock));
    EXPECT_FALSE(st.hasHardCc());
    EXPECT_EQ(st.dr[static_cast<int>(CcCategory::Stun)].applications, 0);

    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 2.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 1.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 0.0f, 1.0e-4f);
    EXPECT_TRUE(st.has(StatusId::Shock));
    EXPECT_TRUE(st.has(StatusId::Stun));
}

TEST(Status_AilmentsCoexist, FiveAilmentsPlusStun)
{
    StatusEffectComponent st{};
    EXPECT_GT(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 0.0f);
    EXPECT_GT(st.applyStatus(StatusId::Poison, 0.0f, 0.0f), 0.0f);
    EXPECT_GT(st.applyStatus(StatusId::Bleed, 0.0f, 0.0f), 0.0f);
    EXPECT_GT(st.applyStatus(StatusId::Ignite, 0.0f, 0.0f), 0.0f);
    EXPECT_GT(st.applyStatus(StatusId::Chill, 0.0f, 0.0f), 0.0f);
    EXPECT_GT(st.applyStatus(StatusId::Shock, 0.0f, 0.0f), 0.0f);
    EXPECT_TRUE(st.has(StatusId::Stun));
    EXPECT_TRUE(st.has(StatusId::Poison));
    EXPECT_TRUE(st.has(StatusId::Bleed));
    EXPECT_TRUE(st.has(StatusId::Ignite));
    EXPECT_TRUE(st.has(StatusId::Chill));
    EXPECT_TRUE(st.has(StatusId::Shock));
    EXPECT_EQ(st.count, 6);
}

TEST(Status_StunDelegatesToApplyCc_Dr, DurationsMatch2_1_0)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 2.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 1.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 0.0f, 1.0e-4f);
    EXPECT_TRUE(st.hasHardCc());
    EXPECT_TRUE(st.hasCategory(CcCategory::Stun));
    EXPECT_EQ(st.dr[static_cast<int>(CcCategory::Stun)].applications, 3);

    StatusFxEvent evs[8]{};
    const int     n = drainStatusFx(st, evs, 8);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].op, StatusFxOp::Applied);
    EXPECT_EQ(evs[0].statusId, static_cast<uint8_t>(StatusId::Stun));
}

TEST(Status_PoisonDoesNotConsumeCcDr, ImmuneStunStillTakesPoison)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 2.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 1.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 0.0f, 1.0e-4f);
    EXPECT_NEAR(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 8.0f, 1.0e-4f);
    EXPECT_TRUE(st.has(StatusId::Poison));
    EXPECT_EQ(st.dr[static_cast<int>(CcCategory::Stun)].applications, 3);
}

TEST(Status_StunAndPoisonCoexist, BothHas)
{
    StatusEffectComponent st{};
    EXPECT_GT(st.applyStatus(StatusId::Stun, 2.0f, 1.0f), 0.0f);
    EXPECT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    EXPECT_TRUE(st.has(StatusId::Stun));
    EXPECT_TRUE(st.has(StatusId::Poison));
    EXPECT_TRUE(st.hasHardCc());
}

TEST(Status_HarvestDot_EmitsDotTick, PoisonInterval)
{
    StatusEffectComponent st{};
    const Entity          src{ makeEntityID(4, 1) };
    const Entity          self{ makeEntityID(5, 1) };
    const StatusDef*      poison = statusDef(StatusId::Poison);
    ASSERT_NE(poison, nullptr);
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f, src), 0.0f);
    drainStatusFx(st, nullptr, 0);

    st.tick(1.0f);
    DamageEvent evs[4]{};
    const int   n = st.harvestDot(evs, 4, self);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].type, DamageType::Poison);
    EXPECT_NEAR(evs[0].amount, poison->tickDamage, 1.0e-4f);
    EXPECT_EQ(evs[0].flags, DamageFlags::DotTick);
    EXPECT_EQ(evs[0].statusId, 0);
    EXPECT_EQ(evs[0].source.id(), src.id());
    EXPECT_EQ(evs[0].target.id(), self.id());
    EXPECT_TRUE(st.has(StatusId::Poison));
}

TEST(Status_HarvestDot_LastTickOnExpiry, OneEventThenEmpty)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    ASSERT_EQ(st.count, 1);
    st.slots[0].remaining = 0.1f;
    st.slots[0].tickAcc   = 0.95f;
    st.tick(0.2f);
    EXPECT_NEAR(st.slots[0].remaining, -0.1f, 1.0e-4f);
    EXPECT_NEAR(st.slots[0].tickAcc, 1.15f, 1.0e-4f);
    EXPECT_EQ(st.count, 1);
    EXPECT_FALSE(st.has(StatusId::Poison));

    DamageEvent evs[4]{};
    const int   n = st.harvestDot(evs, 4, Entity{});
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].flags, DamageFlags::DotTick);
    EXPECT_EQ(evs[0].type, DamageType::Poison);
    EXPECT_NEAR(evs[0].amount, 4.0f, 1.0e-4f);
    EXPECT_EQ(st.count, 0);
}

TEST(Status_ApplyStatus_ZeroDurationUsesDefault, PoisonEightSeconds)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyStatus(StatusId::Poison, 0.0f, 0.0f), 8.0f, 1.0e-4f);
    EXPECT_NEAR(st.remaining(StatusId::Poison), 8.0f, 1.0e-4f);
}

TEST(Status_Reset_ClearsAndFxCleared, DrainSeesCleared)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    st.reset();
    EXPECT_EQ(st.count, 0);
    EXPECT_FALSE(st.has(StatusId::Poison));
    EXPECT_NEAR(st.now, 0.0f, 1.0e-6f);
    EXPECT_EQ(st.dr[static_cast<int>(CcCategory::Stun)].applications, 0);

    StatusFxEvent evs[8]{};
    const int     n = drainStatusFx(st, evs, 8);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].op, StatusFxOp::Cleared);
    EXPECT_EQ(evs[0].statusId, 0);
}

TEST(Status_FxRing_AppliedExpired, TickDoesNotCompact)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    StatusFxEvent evs[StatusEffectComponent::kMaxFxEvents]{};
    int           n = drainStatusFx(st, evs, StatusEffectComponent::kMaxFxEvents);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].op, StatusFxOp::Applied);
    EXPECT_EQ(evs[0].statusId, static_cast<uint8_t>(StatusId::Poison));

    st.tick(8.1f);
    EXPECT_EQ(st.count, 1);
    EXPECT_FALSE(st.has(StatusId::Poison));
    n = drainStatusFx(st, evs, StatusEffectComponent::kMaxFxEvents);
    EXPECT_EQ(n, 0);

    DamageEvent dots[8]{};
    st.harvestDot(dots, 8, Entity{});
    n = drainStatusFx(st, evs, StatusEffectComponent::kMaxFxEvents);
    ASSERT_GE(n, 1);
    EXPECT_EQ(evs[n - 1].op, StatusFxOp::Expired);
    EXPECT_EQ(evs[n - 1].statusId, static_cast<uint8_t>(StatusId::Poison));
    EXPECT_EQ(st.count, 0);
}

TEST(Status_FxRing_TickedOnHarvest, PoisonTickEmitsTicked)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    drainStatusFx(st, nullptr, 0);
    st.tick(1.0f);
    DamageEvent dots[2]{};
    EXPECT_EQ(st.harvestDot(dots, 2, Entity{}), 1);
    EXPECT_TRUE(drainHasOp(st, StatusFxOp::Ticked, static_cast<uint8_t>(StatusId::Poison)));
}

TEST(Status_Overflow_RefuseAilment, FullSlotsReturnZero)
{
    StatusEffectComponent st{};
    st.count = StatusEffectComponent::kMaxStatus;
    for (int i = 0; i < StatusEffectComponent::kMaxStatus; ++i)
    {
        st.slots[i].id        = 200;
        st.slots[i].remaining = 10.0f;
        st.slots[i].category  = CcCategory::Count;
    }
    EXPECT_FLOAT_EQ(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    EXPECT_FALSE(st.has(StatusId::Poison));
}

TEST(Status_ApplyStatus_UnknownAndNone, ReturnsZero)
{
    StatusEffectComponent st{};
    EXPECT_FLOAT_EQ(st.applyStatus(StatusId::None, 8.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(st.applyStatus(static_cast<StatusId>(255), 8.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(st.applyStatus(StatusId::Count, 8.0f, 1.0f), 0.0f);
    EXPECT_EQ(st.count, 0);
    EXPECT_EQ(statusDef(StatusId::None), nullptr);
    EXPECT_EQ(statusDef(StatusId::Count), nullptr);
    ASSERT_NE(statusDef(StatusId::Poison), nullptr);
}

TEST(Status_ApplyCc_ZeroIdStoresStatusIdForCc, AppliedKnockdown)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyCc(CcCategory::Knockdown, 1.4f, true, 0, 0.0f), 1.4f, 1.0e-4f);
    ASSERT_EQ(st.count, 1);
    EXPECT_EQ(st.slots[0].id, static_cast<uint8_t>(statusIdForCc(CcCategory::Knockdown)));
    EXPECT_EQ(st.slots[0].id, static_cast<uint8_t>(StatusId::Knockdown));
    StatusFxEvent evs[4]{};
    const int     n = drainStatusFx(st, evs, 4);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].op, StatusFxOp::Applied);
    EXPECT_EQ(evs[0].statusId, static_cast<uint8_t>(StatusId::Knockdown));
}

TEST(Status_ApplyCc_DrImmunePushesNothing, SecondNotLongerThirdImmune)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyCc(CcCategory::Stun, 2.0f, true, 0, 1.0f), 2.0f, 1.0e-4f);
    drainStatusFx(st, nullptr, 0);

    EXPECT_NEAR(st.applyCc(CcCategory::Stun, 2.0f, true, 0, 1.0f), 1.0f, 1.0e-4f);
    StatusFxEvent evs[4]{};
    EXPECT_EQ(drainStatusFx(st, evs, 4), 0);

    EXPECT_NEAR(st.applyCc(CcCategory::Stun, 2.0f, true, 0, 1.0f), 0.0f, 1.0e-4f);
    EXPECT_EQ(drainStatusFx(st, evs, 4), 0);
    EXPECT_EQ(st.dr[static_cast<int>(CcCategory::Stun)].applications, 3);
}

TEST(Status_ApplyCc_RefreshWhenLonger, PushesRefreshed)
{
    StatusEffectComponent st{};
    EXPECT_NEAR(st.applyCc(CcCategory::Stun, 0.5f, true, 0, 1.0f), 0.5f, 1.0e-4f);
    drainStatusFx(st, nullptr, 0);
    EXPECT_NEAR(st.applyCc(CcCategory::Stun, 2.0f, true, 0, 1.0f), 1.0f, 1.0e-4f);
    StatusFxEvent evs[4]{};
    const int     n = drainStatusFx(st, evs, 4);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(evs[0].op, StatusFxOp::Refreshed);
    EXPECT_EQ(evs[0].statusId, static_cast<uint8_t>(StatusId::Stun));
}

TEST(Status_HarvestDot_NullOutStillCompacts, ExpiredGhostsClear)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Poison, 8.0f, 1.0f), 0.0f);
    st.slots[0].remaining = 0.1f;
    st.tick(0.2f);
    EXPECT_EQ(st.count, 1);
    EXPECT_EQ(st.harvestDot(nullptr, 4, Entity{}), 0);
    EXPECT_EQ(st.count, 0);
}

TEST(Status_HarvestDot_IgniteFireSlashBleed, CatalogDamageTypes)
{
    StatusEffectComponent st{};
    ASSERT_GT(st.applyStatus(StatusId::Ignite, 0.0f, 0.0f), 0.0f);
    ASSERT_GT(st.applyStatus(StatusId::Bleed, 0.0f, 0.0f), 0.0f);
    st.tick(1.0f);
    DamageEvent evs[4]{};
    const int   n = st.harvestDot(evs, 4, Entity{});
    EXPECT_EQ(n, 2);
    bool sawFire  = false;
    bool sawSlash = false;
    for (int i = 0; i < n; ++i)
    {
        EXPECT_EQ(evs[i].flags, DamageFlags::DotTick);
        EXPECT_NEAR(evs[i].amount, 2.0f, 1.0e-4f);
        if (evs[i].type == DamageType::Fire)
            sawFire = true;
        if (evs[i].type == DamageType::Slash)
            sawSlash = true;
    }
    EXPECT_TRUE(sawFire);
    EXPECT_TRUE(sawSlash);
}

TEST(Status_Tick_ExpiredCcUnlocksWithoutCompact, HarvestEmitsExpired)
{
    StatusEffectComponent st{};
    ASSERT_NEAR(st.applyCc(CcCategory::Stun, 0.1f, true, 0, 1.0f), 0.1f, 1.0e-4f);
    st.tick(0.2f);
    EXPECT_EQ(st.count, 1);
    EXPECT_FALSE(st.hasHardCc());
    EXPECT_FALSE(st.hasCategory(CcCategory::Stun));
    drainStatusFx(st, nullptr, 0);
    DamageEvent evs[1]{};
    EXPECT_EQ(st.harvestDot(evs, 1, Entity{}), 0);
    EXPECT_EQ(st.count, 0);
    EXPECT_TRUE(drainHasOp(st, StatusFxOp::Expired, static_cast<uint8_t>(StatusId::Stun)));
}

TEST(Status_Maps_CcRoundTrip, StatusIdForCc)
{
    EXPECT_EQ(statusIdForCc(CcCategory::Stun), StatusId::Stun);
    EXPECT_EQ(statusIdForCc(CcCategory::Root), StatusId::Root);
    EXPECT_EQ(statusIdForCc(CcCategory::Fear), StatusId::Fear);
    EXPECT_EQ(statusIdForCc(CcCategory::Knockdown), StatusId::Knockdown);
    EXPECT_EQ(statusIdForCc(CcCategory::Count), StatusId::None);
    EXPECT_TRUE(statusIdIsCc(StatusId::Stun));
    EXPECT_FALSE(statusIdIsCc(StatusId::Poison));
    EXPECT_EQ(ccCategoryForStatus(StatusId::Knockdown), CcCategory::Knockdown);
    EXPECT_EQ(ccCategoryForStatus(StatusId::Chill), CcCategory::Count);
}
