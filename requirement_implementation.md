# Requirement Implementation Spec (No Code Yet)

This document translates `requirement.md` into an implementation-ready contract.
It is intentionally scoped to planning/spec only. Runtime code is not changed by this document.

## 1) Locked Scope

- HP damage uses two layers:
  - Layer 1: flat defense with clamped result per component
  - Layer 2: multiplicative mitigation percentages
- Physical damage types:
  - `Standard`, `Strike`, `Slash`, `Pierce`
- Elemental damage types:
  - `Fire`, `Frost`, `Poison`, `Lightning`, `Magic` (generic)
- Layer 1 treats all physical subtypes as one physical bucket.
- Layer 2 uses subtype-specific physical mitigation buckets (e.g. slash hit uses slash mitigation).
- Race resistance applies in Layer 2 only, and only to relevant elemental type.
- Status pop damage bypasses Layer 1/Layer 2.
- Poison/Rot DOT ticks also bypass Layer 1/Layer 2.
- Boss detection is keyword-based, using a placeholder keyword ID for now.

## 2) Placeholder IDs

- Boss keyword placeholder: `ERCF.Actor.Boss`  
  Replace this with the exact editor ID when finalized.

## 3) HP Damage Pipeline

### 3.1 Inputs Per Hit

- Incoming components (`rawDamage[type]`):
  - Physical subtype component(s): `Standard/Strike/Slash/Pierce`
  - Elemental component(s): `Fire/Frost/Poison/Lightning/Magic`
- Defender stats:
  - `level`
  - `health`, `magicka` (effective values per chosen source)
  - `armorRating`
- Layer 2 mitigation sources:
  - armor, jewelry, perks, race resistances, other % reductions

### 3.2 Layer 1 Defense

#### Base defense contribution

- `BaseDefense = 10 + (level + 78) / 2.483`

#### Additional level contribution (intentionally additive with BaseDefense)

- Piecewise increment (same behavior as requirement):
  - Level 1..71: +0.40 per level
  - Level 72..91: +1.00 per level
  - Level 92..160: +0.21 per level
  - Level 161..713: +0.036 per level

#### Attribute and armor contributions

- Physical defense extras:
  - `+ 0.1 * (health / 10)`
  - `+ 0.1 * armorRating`
- Elemental defense extras:
  - `+ 0.1 * (magicka / 10)`

#### Final Layer 1 defense buckets

- `DefensePhysicalL1 = BaseDefense + LevelIncrement + HealthTerm + ArmorTerm`
- `DefenseElemL1(T) = BaseDefense + LevelIncrement + MagickaTerm`
  - for `T in {Fire,Frost,Poison,Lightning,Magic}`

#### Layer 1 component result

For each component `c`:

- if physical subtype (`Standard/Strike/Slash/Pierce`): use `DefensePhysicalL1`
- if elemental subtype: use matching `DefenseElemL1(T)`

Then:

- `D1(c) = clamp(raw(c) - defense(c), 0.1 * raw(c), 0.9 * raw(c))`

### 3.3 Layer 2 Mitigation

Layer 2 is applied per component after Layer 1.

- `D2(c) = D1(c) * Π(1 - N_i(c))`

Where:

- `N_i(c)` are mitigation percentages relevant to the component type
- Physical subtype mitigation is bucket-specific:
  - Slash component uses slash mitigation
  - Strike uses strike mitigation
  - Pierce uses pierce mitigation
  - Standard uses standard mitigation
- Elemental mitigation is type-specific:
  - Fire uses fire mitigation
  - Frost uses frost mitigation
  - Poison uses poison mitigation
  - Lightning uses lightning mitigation
  - Magic uses magic mitigation
- Race resistance is included here as one of `N_i(c)` for matching elemental types only.
- Contextual modifiers (also in Layer 2):
  - Silver physical bonus:
    - applies to all physical subtypes when source has `WeaponMaterialSilver` and target has `ActorTypeUndead`
  - Sun damage:
    - treated as separate split component of `Magic` type
    - source tags: `DLC1SunDamage` or `DLC1SunDamageUndead`
    - non-undead target: multiply by `0.0`
    - undead target: multiply by `1.0`

#### Final HP damage

- `DamageHP = Σ D2(c)` for all components in the hit.

## 4) Status Damage Rules

### 4.1 Status thresholds (before pop)

- Status uses per-target resistance thresholds:
  - `Robustness`: Bleed, Frostbite
  - `Immunity`: Poison, Rot
  - `Focus`: Sleep
  - `Madness` (custom bucket): Madness
- Threshold values are derived from max stats and can be increased by armor/jewelry enchantments.

### 4.1.1 Robustness

- Base from max HP.
- Proc-cap progression:
  - 1st proc cap: `300`
  - 2nd proc cap: `450`
  - 3rd+ proc cap: `600`
- Enchantment bonuses increase robustness, then final threshold is capped (cap applies after all bonuses).

### 4.1.2 Immunity

- Base: `(maxHP + maxSP) * 0.5`
- Enchantment bonuses increase immunity.

### 4.1.3 Focus

- Base: `maxMP`
- Enchantment bonuses increase focus.

### 4.1.4 Madness resistance

- Base: `(maxHP + maxMP) * 0.5`
- Enchantment bonuses increase madness resistance.

### 4.2 Status effect outcomes and bypass policy

- Status pop/burst damage is not reduced by Layer 1/Layer 2.
- Poison and Rot DOT ticks also bypass Layer 1/Layer 2.

### 4.2.1 Bleed

- Damage:
  - player + standard NPC: `10% weapon physical damage + 15% maxHP`
  - boss: `10% weapon physical damage + 7.5% maxHP`

### 4.2.2 Frostbite

- Damage:
  - player + standard NPC: `5% weapon damage + 10% maxHP`
  - boss: `5% weapon damage + 7% maxHP`
- Secondary effects:
  - target takes `+20%` damage from all sources for `30s`
  - reduced stamina recovery for `30s`
  - during buildup, incoming Fire elemental damage decreases Frostbite buildup
  - on Frostbite pop, Frostbite status is instantly cleared

### 4.2.3 Madness

- Player-only burst:
  - `10% maxHP + 20 flat HP`
  - instant drain `10% maxMP + 30 flat MP`

### 4.2.4 Poison

- DOT:
  - `0.07% maxHP + 7 flat` per second, `90s`
  - first tick at `t=1s`

### 4.2.5 Rot

- DOT:
  - `0.18% maxHP + 10 flat` per second, `90s`
  - first tick at `t=1s`

### 4.2.6 Sleep

- Damage: `0`
- player + standard NPC only:
  - drain all MP
  - paralyze `5s` (visual/control can use MGEF)
- boss:
  - disabled (no Sleep application)

### 4.3 Boss scaling

- Boss variation is keyed by:
  - placeholder keyword `ERCF.Actor.Boss`

## 5) Stamina Damage on Block

- `BaseStaminaDamage = 10 + (weaponWeight * 1.2)`
- Motion value multipliers:
  - Light: `1.0`
  - Power: `1.5`
  - Sprint: `1.5`
  - Sprint Power: `2.0`
- Incoming block stamina damage:
  - `IncomingStamina = BaseStaminaDamage * MotionValueMultiplier`
- Guard point:
  - shield user: based on shield armor value
  - otherwise: based on defending weapon stamina damage formula
- Final:
  - `StaminaDamageReceived = max(0, IncomingStamina - GuardPoint)`

## 6) Required Data Contract (ESP/Runtime)

- Damage subtype tags on attack sources:
  - physical subtype tag(s) and elemental type tag(s)
- Layer 2 mitigation values per bucket:
  - standard/strike/slash/pierce
  - fire/frost/poison/lightning/magic
- Race elemental resistance entries (mapped into layer-2 elemental mitigation)
- Contextual keyword checks:
  - source: `WeaponMaterialSilver`
  - target: `ActorTypeUndead`
  - source: `DLC1SunDamage` and `DLC1SunDamageUndead`
- Boss keyword assignment:
  - `ERCF.Actor.Boss` (placeholder)

## 7) Open Item

- Magicka damage model remains TBD and is out of current implementation scope.

