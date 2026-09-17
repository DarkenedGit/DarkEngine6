# Combat — one-page cheatsheet

Full RFC: [DESIGN-combat-system.md](./DESIGN-combat-system.md). Tip baseline `19a5ea25`.

## Default stack

1. **Melee:** montage notifies → capsule **sweeps** → **HitSet** → ordered resolve
2. **Ranged:** **Hybrid** (hitscan truth + tracer); ballistic when travel/lead matters
3. **Magic:** `SpellDef` + windup/channel/release/recovery
4. **Damage:** Slash/Pierce/Blunt + elementals + **True**; \(d\cdot K/(A_{\mathrm{eff}}+K)\), DR cap **0.75**, \(A_{\mathrm{eff}}=A(1-p)\)
5. **Defense:** block arc + stamina; short perfect-parry; **poise**; hyper-armor on heavies
6. **CC:** categories; DR **1.0 → 0.5 → immune**; hard > soft

## Resolve order

Filter → i-frame → parry → block → armor/resist → poise → HP → CC/DR → react → knockback → hitstop/cues → HitSet

## Melee windows

| Phase | Hitbox |
|-------|--------|
| Startup | Off |
| Active | On + sweep prev→curr |
| Recovery | Off |
| Cancel / i-frame | Author notifies |

HitSet clear on Active Begin; first frame sample-only.

## Severity

Flinch → Stagger → Stun → Knockdown → Launch  
(from dmg norm + poise break + attacker tags + direction)

## Build order (short)

1. DamageEvent + CombatSystem  
2. Types + armor/resist  
3. Severity reaction table  
4. Poise  
5. Block/parry  
6. Melee sweeps + HitSet  
7. Status/CC DR  
8. Magic SpellDef  
9. (opt) weak spots  
10. Net replicate events + vitals  

## Explode list (do not ship)

- Always-on weapon collision / no HitSet  
- Discrete melee only → tunneling  
- Client-auth damage  
- Uncapped armor DR  
- 360° stamina-free block / parry = block length  
- Poise regen mid-combo with no delay  
- Hyper armor on every light  
- Stacking independent stuns forever  
- Copying FromSoftware or WoW patch numbers as DE6 constants  
