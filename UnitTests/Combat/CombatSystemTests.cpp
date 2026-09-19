#include <gtest/gtest.h>

#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Character/HitReaction.h"
#include "Combat/ArmorComponent.h"
#include "Combat/AttackDef.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/HitSet.h"
#include "Combat/PoiseComponent.h"
#include "Combat/SpellCaster.h"
#include "Combat/StatusEffectComponent.h"
#include "ECS/World.h"
#include "Math/MathHelper.h"

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

namespace
{
    DamageEvent makeHit(float amount, DamageType type = DamageType::Slash)
    {
        DamageEvent ev{};
        ev.amount   = amount;
        ev.type     = type;
        ev.hitDir   = Vector3f{ 0.0f, 0.0f, 1.0f };
        ev.flags    = DamageFlags::CanBlock | DamageFlags::CanParry;
        return ev;
    }
}

TEST(Combat_TrueIgnoresArmor, FinalEqualsAmount)
{
    CombatSystem sys;
    Health       hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    ArmorComponent armor{};
    armor.stats.armor = 1000.0f;
    DamageEvent ev    = makeHit(40.0f, DamageType::True);
    const ResolveResult r = sys.resolveDirect(ev, &hp, nullptr, nullptr, &armor, nullptr, nullptr, nullptr, hp.maxHp());
    EXPECT_TRUE(r.applied);
    EXPECT_FLOAT_EQ(r.finalDamage, 40.0f);
    EXPECT_FLOAT_EQ(hp.hp(), 60.0f);
}

TEST(Combat_HyperbolicArmor_Mid, DrMatchesCurveAndCap)
{
    // A=100, K=100 → DR=0.5 → final=50 from 100.
    EXPECT_NEAR(hyperbolicDamageReduction(100.0f), 0.5f, 1.0e-5f);
    EXPECT_NEAR(mitigatePhysical(100.0f, 100.0f, 0.0f), 50.0f, 1.0e-4f);

    // Cap: huge armor still ≤ 0.75 DR.
    const float drHuge = hyperbolicDamageReduction(1.0e6f);
    EXPECT_NEAR(drHuge, kDamageReductionCap, 1.0e-5f);
    EXPECT_NEAR(mitigatePhysical(100.0f, 1.0e6f, 0.0f), 25.0f, 1.0e-3f);

    CombatSystem sys;
    Health       hp{ HealthSettings{ 200.0f, 0.0f, 99.0f } };
    ArmorComponent armor{};
    armor.stats.armor     = 100.0f;
    DamageEvent ev        = makeHit(100.0f, DamageType::Slash);
    const ResolveResult r = sys.resolveDirect(ev, &hp, nullptr, nullptr, &armor, nullptr, nullptr, nullptr, hp.maxHp());
    EXPECT_NEAR(r.finalDamage, 50.0f, 1.0e-3f);
}

TEST(Combat_ArmorPen_ReducesA, HigherFinalThanNoPen)
{
    const float noPen = mitigatePhysical(100.0f, 100.0f, 0.0f);
    const float pen   = mitigatePhysical(100.0f, 100.0f, 0.5f); // A_eff=50 → DR=1/3 → ~66.67
    EXPECT_GT(pen, noPen);
    EXPECT_NEAR(pen, 100.0f * (100.0f / (50.0f + 100.0f)), 1.0e-3f);

    CombatSystem sys;
    Health       hpA{ HealthSettings{ 200.0f, 0.0f, 99.0f } };
    Health       hpB{ HealthSettings{ 200.0f, 0.0f, 99.0f } };
    ArmorComponent armor{};
    armor.stats.armor = 100.0f;
    DamageEvent a     = makeHit(100.0f);
    DamageEvent b     = makeHit(100.0f);
    b.armorPen        = 0.5f;
    const float fa    = sys.resolveDirect(a, &hpA, nullptr, nullptr, &armor, nullptr, nullptr, nullptr, 200.0f).finalDamage;
    const float fb    = sys.resolveDirect(b, &hpB, nullptr, nullptr, &armor, nullptr, nullptr, nullptr, 200.0f).finalDamage;
    EXPECT_GT(fb, fa);
}

TEST(Combat_ElementalResist, FireTimesOneMinusR)
{
    ArmorStats stats{};
    stats.resist[static_cast<size_t>(DamageType::Fire)] = 0.40f;
    EXPECT_NEAR(mitigateTyped(100.0f, DamageType::Fire, stats, 0.0f, false), 60.0f, 1.0e-4f);

    CombatSystem sys;
    Health       hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    ArmorComponent armor{};
    armor.stats = stats;
    DamageEvent ev = makeHit(50.0f, DamageType::Fire);
    const ResolveResult r = sys.resolveDirect(ev, &hp, nullptr, nullptr, &armor, nullptr, nullptr, nullptr, 100.0f);
    EXPECT_NEAR(r.finalDamage, 30.0f, 1.0e-3f);
}

TEST(Combat_IFrame_DropsEvent, AppliedFalse)
{
    CombatSystem sys;
    Health       hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    DefenseComponent def{};
    def.beginIFrame(0.5f);
    DamageEvent ev = makeHit(25.0f);
    const ResolveResult r = sys.resolveDirect(ev, &hp, nullptr, &def, nullptr, nullptr, nullptr, nullptr, 100.0f);
    EXPECT_TRUE(r.iframe);
    EXPECT_FALSE(r.applied);
    EXPECT_FLOAT_EQ(hp.hp(), 100.0f);
}

TEST(Combat_Block_ArcAndMitigation, OutsideFullInsideReduced)
{
    CombatSystem sys;
    Health       hpIn{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    Health       hpOut{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    DefenseComponent def{};
    def.blocking         = true;
    def.blockArcDeg      = 120.0f;
    def.stamina          = 100.0f;
    def.blockStaminaCost = 10.0f;
    ArmorComponent armor{};
    armor.stats.blockMitigation = 0.5f;

    Vector3f facing{ 0.0f, 0.0f, 1.0f }; // look +Z
    // Frontal hit: attacker in front → hitDir +Z (attacker→victim along +Z means attack from -Z... 
    // facing +Z, fromAttacker = -hitDir. For frontal, fromAttacker should align with facing → hitDir = -facing = -Z
    DamageEvent frontal = makeHit(40.0f);
    frontal.hitDir      = Vector3f{ 0.0f, 0.0f, -1.0f };
    DefenseComponent defIn = def;
    const ResolveResult rin = sys.resolveDirect(frontal, &hpIn, nullptr, &defIn, &armor, nullptr, nullptr, &facing, 100.0f);
    EXPECT_TRUE(rin.blocked);
    EXPECT_NEAR(rin.finalDamage, 20.0f, 1.0e-3f); // 40 * 0.5 block, no armor
    EXPECT_NEAR(defIn.stamina, 90.0f, 1.0e-3f);

    DamageEvent rear = makeHit(40.0f);
    rear.hitDir      = Vector3f{ 0.0f, 0.0f, 1.0f }; // from behind
    DefenseComponent defOut = def;
    const ResolveResult rout = sys.resolveDirect(rear, &hpOut, nullptr, &defOut, &armor, nullptr, nullptr, &facing, 100.0f);
    EXPECT_FALSE(rout.blocked);
    EXPECT_NEAR(rout.finalDamage, 40.0f, 1.0e-3f);
    EXPECT_NEAR(defOut.stamina, 100.0f, 1.0e-3f);
}

TEST(Combat_PerfectParry, NegatesAndFlags)
{
    CombatSystem sys;
    Health       hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    DefenseComponent def{};
    def.beginParryWindow(0.2f);
    def.blockArcDeg = 140.0f;
    Vector3f facing{ 0.0f, 0.0f, 1.0f };
    DamageEvent ev = makeHit(50.0f);
    ev.hitDir      = Vector3f{ 0.0f, 0.0f, -1.0f };
    const ResolveResult r = sys.resolveDirect(ev, &hp, nullptr, &def, nullptr, nullptr, nullptr, &facing, 100.0f);
    EXPECT_TRUE(r.parried);
    EXPECT_TRUE(r.applied);
    EXPECT_FLOAT_EQ(r.finalDamage, 0.0f);
    EXPECT_FLOAT_EQ(hp.hp(), 100.0f);
}

TEST(Combat_PoiseBreak_ForcesStagger, SeverityAtLeastCritical)
{
    CombatSystem sys;
    Health       hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    PoiseComponent poise{};
    poise.maxPoise = 20.0f;
    poise.poise    = 20.0f;
    HitReaction hit{};
    DamageEvent ev = makeHit(5.0f); // ~5% → Light, but poise break upgrades
    ev.poiseDamage = 25.0f;
    const ResolveResult r = sys.resolveDirect(ev, &hp, &hit, nullptr, nullptr, &poise, nullptr, nullptr, 100.0f);
    EXPECT_TRUE(r.poiseBroke);
    EXPECT_GE(static_cast<int>(r.severity), static_cast<int>(Severity::Critical));
    EXPECT_TRUE(hit.stunned());
}

TEST(Combat_HitSet_NoDoubleHit, SecondOverlapIgnored)
{
    HitSet set;
    Entity a{ makeEntityID(1, 1) };
    Entity b{ makeEntityID(2, 1) };
    EXPECT_TRUE(set.tryAdd(a));
    EXPECT_FALSE(set.tryAdd(a));
    EXPECT_TRUE(set.tryAdd(b));
    EXPECT_EQ(set.count(), 2u);
    set.clear();
    EXPECT_TRUE(set.tryAdd(a));
}

TEST(Combat_CcDr_SecondHalf_ThirdImmune, DurationsMatchTable)
{
    StatusEffectComponent st{};
    const float d1 = st.applyCc(CcCategory::Stun, 2.0f, true, 1, 1.0f);
    EXPECT_NEAR(d1, 2.0f, 1.0e-4f);
    const float d2 = st.applyCc(CcCategory::Stun, 2.0f, true, 1, 1.0f);
    EXPECT_NEAR(d2, 1.0f, 1.0e-4f);
    const float d3 = st.applyCc(CcCategory::Stun, 2.0f, true, 1, 1.0f);
    EXPECT_NEAR(d3, 0.0f, 1.0e-4f);

    CombatSystem sys;
    Health       hp{ HealthSettings{ 100.0f, 0.0f, 99.0f } };
    StatusEffectComponent st2{};
    DamageEvent ev = makeHit(1.0f);
    ev.flags |= DamageFlags::HardCc;
    ev.statusDuration = 2.0f;
    auto r1 = sys.resolveDirect(ev, &hp, nullptr, nullptr, nullptr, nullptr, &st2, nullptr, 100.0f);
    auto r2 = sys.resolveDirect(ev, &hp, nullptr, nullptr, nullptr, nullptr, &st2, nullptr, 100.0f);
    auto r3 = sys.resolveDirect(ev, &hp, nullptr, nullptr, nullptr, nullptr, &st2, nullptr, 100.0f);
    EXPECT_NEAR(r1.ccDuration, 2.0f, 1.0e-4f);
    EXPECT_NEAR(r2.ccDuration, 1.0f, 1.0e-4f);
    EXPECT_NEAR(r3.ccDuration, 0.0f, 1.0e-4f);
}

TEST(Combat_SeverityBands, NormMapsToExpected)
{
    EXPECT_EQ(severityFromNormalized(0.01f, false, false), Severity::Tick);
    EXPECT_EQ(severityFromNormalized(0.05f, false, false), Severity::Light);
    EXPECT_EQ(severityFromNormalized(0.12f, false, false), Severity::Medium);
    EXPECT_EQ(severityFromNormalized(0.25f, false, false), Severity::Heavy);
    EXPECT_EQ(severityFromNormalized(0.40f, false, false), Severity::Critical);
    EXPECT_EQ(severityFromNormalized(0.05f, true, false), Severity::Critical);
    EXPECT_EQ(severityFromNormalized(0.01f, false, true), Severity::Fatal);

    const HitReactionSettings heavy = hitReactionForSeverity(Severity::Heavy);
    const HitReactionSettings light = hitReactionForSeverity(Severity::Light);
    EXPECT_GT(heavy.stunSeconds, light.stunSeconds);
    EXPECT_GT(heavy.knockbackDistance, light.knockbackDistance);
}

TEST(Spell_InterruptOnHardCc, ChannelCancelled)
{
    SpellDef def{};
    def.windupSeconds    = 0.5f;
    def.channelSeconds   = 1.0f;
    def.releaseSeconds   = 0.05f;
    def.recoverySeconds  = 0.1f;
    def.manaCost         = 10.0f;
    def.commitCostOnRelease = true;
    def.interruptPolicy  = SpellInterruptPolicy::HardCc;
    SpellCaster caster{ def };
    caster.setMana(100.0f, 100.0f);
    SpellCastRequest req{};
    req.origin    = Vector3f{ 0, 0, 0 };
    req.direction = Vector3f{ 0, 0, 1 };
    ASSERT_TRUE(caster.beginCast(req));
    EXPECT_EQ(caster.phase(), SpellPhase::Windup);
    caster.tick(0.6f);
    EXPECT_EQ(caster.phase(), SpellPhase::Channel);
    EXPECT_TRUE(caster.interrupt(true));
    EXPECT_EQ(caster.phase(), SpellPhase::Idle);
    EXPECT_FALSE(caster.interrupt(true)); // already idle
}

TEST(Spell_ProjectileResolvePath, BuildsDamageEvent)
{
    SpellDef def{};
    def.windupSeconds       = 0.0f;
    def.channelSeconds      = 0.0f;
    def.releaseSeconds      = 0.0f;
    def.recoverySeconds     = 0.0f;
    def.manaCost            = 5.0f;
    def.commitCostOnRelease = true;
    def.targeting           = SpellTargeting::Projectile;
    def.damage              = 22.0f;
    def.damageType          = DamageType::Fire;
    def.projectileSpeed     = 50.0f;
    def.range               = 10.0f;
    SpellCaster caster{ def };
    caster.setMana(50.0f, 50.0f);
    SpellCastRequest req{};
    req.caster    = Entity{ makeEntityID(1, 1) };
    req.origin    = Vector3f{ 0, 0, 0 };
    req.direction = Vector3f{ 0, 0, 1 };
    ASSERT_TRUE(caster.beginCast(req));
    caster.tick(0.001f); // windup→release→spawn→recovery
    caster.tick(0.001f);
    EXPECT_TRUE(caster.projectile().alive);
    Entity target{ makeEntityID(2, 1) };
    DamageEvent out{};
    // Place target at projectile position
    ASSERT_TRUE(caster.tryResolveProjectileHit(target, caster.projectile().pos, 1.0f, out));
    EXPECT_EQ(out.target.id(), target.id());
    EXPECT_FLOAT_EQ(out.amount, 22.0f);
    EXPECT_EQ(out.type, DamageType::Fire);
}

TEST(CombatSystem_WorldResolve, UsesHealthComponent)
{
    World world;
    Entity target = world.createEntity();
    HealthComponent hc{};
    hc.health = Health{ HealthSettings{ 80.0f, 0.0f, 99.0f } };
    world.emplace<HealthComponent>(target, std::move(hc));
    world.emplace<ArmorComponent>(target);

    CombatSystem sys;
    DamageEvent ev = makeHit(10.0f, DamageType::True);
    ev.target      = target;
    const ResolveResult r = sys.resolve(world, ev);
    EXPECT_TRUE(r.applied);
    EXPECT_FLOAT_EQ(r.finalDamage, 10.0f);
    EXPECT_NEAR(world.get<HealthComponent>(target)->health.hp(), 70.0f, 1.0e-3f);
}

TEST(AttackDef_Defaults, MeleePayloadFieldsPresent)
{
    AttackDef a{};
    a.damage      = 16.0f;
    a.type        = DamageType::Blunt;
    a.poiseDamage = 20.0f;
    a.armorPen    = 0.1f;
    EXPECT_EQ(a.type, DamageType::Blunt);
    EXPECT_GT(a.poiseDamage, 0.0f);
}
