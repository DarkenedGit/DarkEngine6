# Combat system (DamageEvent pipeline)

| Field | Value |
|-------|-------|
| **Status** | Draft rev 1 |
| **Date** | 2026-09-17 |
| **Tip baseline** | `19a5ea25db046ae6e1484393240c3eff3e3f44e6` (PBR gap RFCs on tip) |
| **Depends on** | Existing `Weapons/`, `Character/Health`, `Character/HitReaction`, `Collision/` (capsule/swept), `Animation/AnimNotify`, Sandbox combat glue |

## Purpose

Freeze an implementable combat stack for DarkEngine6: one **resolve pipeline**, many attack sources (melee, projectile, magic). Docs only in this PR — no gameplay code.

Companion one-pager: [DESIGN-combat-cheatsheet.md](./DESIGN-combat-cheatsheet.md).

Research inputs (workspace, not in-repo): ResearchBot brief `/workspace/darkengine6-combat-system-brief.md`, melee course `/workspace/melee-combat-course.md`.

## Current tip (gap snapshot)

**Present (arcade slice)**
- `Weapons/Weapon.h` — `WeaponHit` (flat `float damage`), `WeaponWorldQuery`, fire/tick
- `MeleeWeapon` — range + facing cone (`minDot`), cooldown; **not** swept blade volumes
- `ProjectileWeapon` — hitscan or ballistic; recoil; impact VFX/audio
- `WeaponLoadout` + ECS component — two slots
- `HittableComponent` — AABB half-extents
- `Character/Health` — HP, delayed regen, death
- `Character/HitReaction` — fixed stun + knockback (global settings)
- `Gameplay/HealthPack`, blood/impact particles, Sandbox `updateCombat` / `onWeaponHit`
- Unit tests under `UnitTests/Weapons`, `UnitTests/Character`

**Missing**
- Magic / mana / `SpellDef`
- Damage types, armor, resists
- Block / parry / poise / hyper-armor
- Status effects + CC diminishing returns
- Severity-banded reactions (flinch → stagger → knockdown)
- Anim-gated melee active windows + HitSet
- Net replication of HP / damage events

**Unused sketches:** `Character/Body.h` + physiology headers — aspirational; **do not** block v1 on organ sim. Optional later fidelity behind the same `DamageEvent`.

## Goals (v1 track)

1. All hits go through `CombatSystem::resolve(DamageEvent)` — weapons/spells never write HP directly long-term.
2. Typed damage + hyperbolic armor + elemental resists.
3. Defense: i-frames, block arc + stamina, short perfect-parry, poise meter.
4. Reactions from type × severity (+ poise break).
5. Melee upgrades to notify-gated capsule sweeps + HitSet.
6. Projectiles carry typed payload; hybrid hitscan+tracer as default for “gun feel.”
7. Magic as data `SpellDef` sharing cast/interrupt grammar with melee.
8. Unit tests for every **Acceptance → Unit** case below.
9. No C++ exceptions; failures `bool` + `DE_LOG_*`.

## Non-goals (v1)

- Competitive FPS full lag-comp rewind (Source-style) — co-op PvE default: server auth, tracer predict only
- Full GAS / GameplayAbility framework port
- Limb/organ simulation (Body sketches)
- Fighting-game discrete boxes as primary melee
- Copying FromSoftware / WoW patch numbers as DE6 constants

## Default stack

1. **Melee:** anim notifies → capsule sweeps → HitSet → ordered resolve
2. **Ranged:** hybrid (hitscan truth + tracer); ballistic when travel/lead matters
3. **Magic:** `SpellDef` windup / channel? / release / recovery + cost/interrupt tags
4. **Damage:** Slash / Pierce / Blunt + elementals + True; \(d\cdot K/(A_{\mathrm{eff}}+K)\), DR cap **0.75**, \(A_{\mathrm{eff}}=A(1-p)\)
5. **Defense/CC:** block arc + stamina; short perfect-parry; poise + hyper-armor; CC DR 100% → 50% → immune

## Resolve order (frozen)

```
filter (team/self/dead)
  → i-frame
  → perfect parry
  → block (arc + stamina)
  → armor / resist (typed)
  → poise damage
  → HP apply
  → CC / status + DR table
  → reaction montage (severity)
  → knockback
  → hitstop / cues / VFX
  → HitSet record
```

## Architecture

```
Attack source (MeleeWeapon | ProjectileWeapon | SpellCaster)
    → DamageEvent
    → CombatSystem::resolve
         ├─ DefenseComponent (guard, i-frames)
         ├─ ArmorComponent / ResistComponent
         ├─ PoiseComponent
         ├─ HealthComponent
         ├─ StatusEffectComponent
         └─ HitReaction (table-driven) + AnimNotify / AI events
```

New module: **`Combat/`** (resolution). Keep **`Weapons/`** as delivery; **`Character/`** as vitals.

### Tip hooks

| Existing | Role after RFC |
|----------|----------------|
| `WeaponHit` | Emit `DamageEvent` (add type, poise, flags) instead of raw float into Health |
| `Health::applyDamage` | Called only from CombatSystem |
| `HitReaction` | Replace fixed settings with severity table lookup |
| `Collision` capsule/swept | Melee active-window sweeps |
| `AnimNotify` | Startup / Active / Recovery / IFrame / HyperArmor / Cancel |
| `WeaponWorldQuery` | Keep pluggable targeting |
| Sandbox `onWeaponHit` | Thin adapter → `CombatSystem::resolve` |

## Public C++ API (sketch)

```cpp
namespace Dark::Combat {

enum class DamageType : uint8_t {
  Slash, Pierce, Blunt,
  Fire, Frost, Lightning, Poison, Arcane, Holy, Shadow,
  True  // ignores armor + resists
};

enum class Severity : uint8_t {
  Tick, Light, Medium, Heavy, Critical, Fatal
};

struct DamageEvent {
  Entity source{};
  Entity target{};
  DamageType type = DamageType::Slash;
  float amount = 0.f;       // pre-mitigation
  float poiseDamage = 0.f;
  float armorPen = 0.f;     // 0..1 → A_eff = A*(1-p)
  Math::Vector3f hitPoint{};
  Math::Vector3f hitDir{};  // attacker → victim
  uint32_t flags = 0;       // CanBlock, CanParry, IgnoresArmor, DotTick, ...
  // optional status payload id / magnitude
};

struct ArmorStats {
  float armor = 0.f;
  float resist[(size_t)DamageType::True]{}; // unused slot for True
  float blockMitigation = 0.7f;
};

struct ResolveResult {
  bool applied = false;
  bool blocked = false;
  bool parried = false;
  bool killed = false;
  float finalDamage = 0.f;
  Severity severity = Severity::Tick;
  bool poiseBroke = false;
};

class CombatSystem {
public:
  ResolveResult resolve(World& world, const DamageEvent& ev);
};

} // namespace Dark::Combat
```

Mitigation (physical):

\[
A_{\mathrm{eff}} = A(1-p),\quad
d' = d\cdot\frac{K}{A_{\mathrm{eff}}+K},\quad
\mathrm{DR}=\frac{A_{\mathrm{eff}}}{A_{\mathrm{eff}}+K}\le 0.75
\]

True → skip armor & resists. Elemental → \(\times(1-R_t)\) with \(R_t\) clamped (e.g. \([-0.25, 0.90]\)).

Tune \(K\) in play so mid armor ≈ 30–40% DR vs on-level trash — **do not** copy WoW tables.

## Melee

| Phase | Hitbox |
|-------|--------|
| Startup | Off |
| Active | On — sweep capsule prev→curr; first armed frame sample-only |
| Recovery | Off |
| Cancel / i-frame / hyper-armor | Author notifies |

**HitSet:** clear on Active begin; record target ids hit this window; optional `multiHitInterval` for spins.

Upgrade path: keep cone melee as fallback until AnimNotify windows exist.

`AttackDef` fields: damage, types[], poiseDamage, staminaCost, hitstop, knockback, teamFilter, maxTargets, armorLevelWhileAttacking, cancelMask, reactionTag.

## Projectiles

| Mode | Truth | Use |
|------|-------|-----|
| Hitscan | Instant ray/sphere | Beams, snappy shots |
| Ballistic | \(v+=g\Delta t\) + segment cast | Arrows, lobs |
| Hybrid **(default)** | Hitscan + tracer VFX | Most guns/spells |

`ProjectileDef` carries same damage/poise/type fields as melee. Net v1: server auth; predict tracers; no full rewind until PvP needs it.

## Magic

```text
SpellDef:
  id, montages
  phases: windup, channel?, release, recovery
  targeting: Self|Ally|Enemy|GroundAoE|Projectile|Cone
  range, radius, angle
  costs: mana, stamina, cooldown
  interruptPolicy: Never | SoftCC | HardCC | DamageOver(N)
  resourceOnInterrupt: RefundPartial | Keep
  projectileId? / aoeId?
  effects[] / tags
```

Commit cost at **Release** for charged spells; at **Windup** for cantrips (per-spell). Hard CC always interrupts when policy allows. Delivery reuses projectile / swept volume → same `DamageEvent`.

## Defense / poise / CC

| Tool | Gate | Cost | Payoff |
|------|------|------|--------|
| Dodge i-frames | notify window | stamina | Ignore hit |
| Block | facing arc (~120–160°) | stamina on impact | HP × blockMitigation; still some poise |
| Perfect parry | short window + arc | low | Negate; stun attacker / riposte grant |
| Poise | separate meter | regen after delay | ≤0 → stagger/knockdown |
| Hyper-armor | notify on heavy Active | design budget | Skip stagger while attacking |

**CC DR** (per target, per category, rolling window): 1st 100% duration → 2nd 50% → 3rd+ immune until reset. Hard CC replaces soft. Clamp max hard CC (~3s) — tune in play.

## Severity → reaction

| Band | Example trigger | Result |
|------|-----------------|--------|
| Tick | DoT / &lt;~3% maxHP | VFX only |
| Light | ~3–8% | Flinch |
| Medium | ~8–18% | Hitstun + small knockback |
| Heavy | ~18–35% | Stagger |
| Critical | &gt;~35% or poise break | Knockdown / launch |
| Fatal | HP→0 | Death; type may tint VFX |

Type overlays: Fire→burn chance; Frost→slow/freeze stacks; Lightning→cast interrupt; Poison→DoT, low knockback; Blunt→extra poise; Slash→bleed chance; Pierce→weak-spot bias.

Score sketch: `s = w_dmg*norm(dmg) + w_poise*broke + w_tag*tagWeight` → first matching `HitReactTable` row + direction suffix.

## HP / resource states

| State | Trigger | Notes |
|-------|---------|-------|
| Healthy | &gt;60% | Normal |
| Hurt | 30–60% | Optional limp / AI bias |
| Critical | &lt;30% | UI/AI tell |
| Downed (optional) | HP=0 + bleed-out mode | Ally revive window |
| Dead | Confirmed | AI `deadFor`, loot |
| Invulnerable | spawn / roll / cinematic | Ignore DamageEvents |

Gate existing delayed regen on `timeSinceDamage` + not InCombat.

## Build order (implementation PRs)

| # | Slice | Done-when |
|---|-------|-----------|
| 1 | `DamageEvent` + `CombatSystem::resolve` | Weapons emit events; Health only via resolve; unit tests |
| 2 | `DamageType` + armor/resist formula | Table tests vs golden floats; DR cap |
| 3 | Severity → HitReaction table | Replaces fixed stun/knockback defaults |
| 4 | Poise / stagger | Meter regen delay; break → Critical reaction |
| 5 | Block + parry + stamina | Arc test; perfect window; chip rules |
| 6 | Melee sweeps + Attack phases + HitSet | Capsule sweep; no double-hit |
| 7 | Status effects + CC DR | Burn/frost/poison/stun/slow; stacking rules |
| 8 | Magic `SpellDef` + caster | Mana; one projectile + one AoE spell in Sandbox |
| 9 | Weak spots / limb mult (optional) | Bridge toward Body sketches |
| 10 | Net: replicate DamageEvents + HP/poise | Reliable events; snapshot vitals |

## Acceptance tests

### Unit

| Test | Expected |
|------|----------|
| `Combat_TrueIgnoresArmor` | final == amount |
| `Combat_HyperbolicArmor_Mid` | DR ≈ A/(A+K) within tol; clamp ≤0.75 |
| `Combat_ArmorPen_ReducesA` | higher final than same hit with p=0 |
| `Combat_ElementalResist` | Fire × (1-R) |
| `Combat_IFrame_DropsEvent` | applied=false |
| `Combat_Block_ArcAndMitigation` | outside arc full dmg; inside reduced + stamina spent |
| `Combat_PerfectParry` | damage 0; attacker flagged |
| `Combat_PoiseBreak_ForcesStagger` | severity ≥ Critical when poise→0 |
| `Combat_HitSet_NoDoubleHit` | second overlap same Active ignored |
| `Combat_CcDr_SecondHalf_ThirdImmune` | durations match table |
| `Combat_SeverityBands` | norm dmg maps to expected band |
| `Spell_InterruptOnHardCc` | channel cancelled per policy |

### Integration / visual

- Sandbox: melee sweep hits once per swing; blocked frontal slash chips; parry opens riposte window
- Projectile hybrid: tracer matches hitscan range
- One fire bolt applies Fire typed damage + optional burn DoT
- AI reacts to `OnDamaged` / `OnStaggered` (assist/flee hooks already exist)

### Negative

- Client-auth “I hit” RPC rejected (net slice)
- Always-on weapon collision without Active notify → fail review
- Uncapped armor DR → fail unit test

## Rollout

1. Land this RFC (docs).
2. Implement slices 1–3 before any new weapon content depends on types.
3. Sandbox adapters stay thin; move logic into `Combat/`.
4. Physiology/`Body` remains optional post-v1.

## Risks

- Rewriting Sandbox combat glue mid-feature — prefer adapter first
- Hitstop on every DoT tick — cap concurrent hitstops
- Launch without juggle policy — infinite air combos
- Inventing GDC talk titles / copying Souls ms windows — author + measure in DE6

## References (ideas, not constants)

- GDKeys — Anatomy of an Attack; Jerry Zhang — Anatomy of an Enemy Attack in Dark Souls 3 (Gamedeveloper)
- Unity CapsuleCast / Raycast; UE traces overview; Godot intersect_ray
- Tranek GASDocumentation; Epic community Lyra health/damage tutorial (GAS *ideas*, not a port)
- Warcraft Wiki armor hyperbolic form; Dreamgrove / Ask Mr. Robot DR discussion
- Gaffer on Games networking; Bernier lag-compensation paper / Source SDK (defer full rewind)
- MaxsuPoise mechanics manual — shape only, thresholds game-specific
