# Damage Model Requirement

## 1) HP Damage Types

There are two top-level HP damage categories:

- **Physical**: `Standard`, `Strike`, `Slash`, `Pierce`
- **Elemental**: `Fire`, `Frost`, `Poison`, `Lightning`, `Magic` (generic)

Design intent:

- Standard: neutral vs most armor types
- Strike: strong vs heavy armor and hard-skin creatures
- Slash: strong vs flesh, cloth, and light armor
- Pierce: good armor penetration (extra 30% damage for layer 1 when the enemy is attacking/sprinting/or there is opening)

## 2) Two-Layer HP Processing

This section describes what the code does today on the **strict override** path (`ComputeRequirementDamageForVictim` in `OverrideDamageHook.cpp`, using `CombatMath.h` for Layer 1 and `EspRouting` for Layer 2 absorption). Other code paths (for example extra elemental HP on `TESHitEvent` when override is off) still use the older `PostDefense` / per-type `mitigation.defense[]` model from `CombatMath.h` and are not identical to the formulas below.

### 2.1 Layer 1 (Flat Defense)

**When ERAS is available** (`TryGetActorSnapshot` succeeds), strict override uses **`PlayerStatsSnapshot::defense`** (`DefenseSheet`) as the stat-derived L1 defense **per damage family**:

- **Physical line:** `defense.physical`
- **Elemental lines** (aligned with `DamageTypeId`): `defense.magic`, `defense.fire`, `defense.frost`, `defense.poison`, `defense.lightning`

ERAS owns the curve; ERCF applies the same clamp to that core only (strict override does **not** add `ERCF.MGEF.Defense` flat values on Layer 1; those still affect non-strict paths where applicable).

**When ERAS is not available**, ERCF synthesizes buckets with `Req_ComputeDefenseBucketsL1(level, maxHP, maxMP, armorRating)`:

- **`physical`**: base + level increment + `0.1 * (maxHP / 10)` + `0.1 * armorRating`
- **`elemental[5]`**: each slot = base + level increment + `0.1 * (maxMP / 10)` (shared formula across the five types; separate array slots).

Fallback **inputs**: `Actor::GetLevel()`, **current** `kHealth` / `kMagicka`, `kDamageResist` as `armorRating` (not summed worn ARMO rating).

#### Base defense (Req fallback only)

`BaseDefense = 10 + (level + 78) / 2.483`

#### Level increment (piecewise, additive; Req fallback only)

Implementation matches `Req_LevelIncrement`: uses `L = max(1, level)` and sums segments `(upper - lo) * slope` for each band:

| Segment (exclusive of `lo`, inclusive of `upper`) | Slope |
|---------------------------------------------------|-------|
| above 1 up to 71                                  | 0.40  |
| above 71 up to 91                                 | 1.00  |
| above 91 up to 160                                | 0.21  |
| above 160 up to 713                               | 0.036 |

At level 1 the increment is 0.

#### Layer 1 defense buckets (strict override)

- **Physical:** `D` = ERAS `defense.physical` **or** Req `physical` bucket only.
- **Elemental:** for each type `T`, `D = core(T)` where `core(T)` is the matching ERAS `DefenseSheet` field **or** the Req elemental slot (Magic … Lightning order).

#### Layer 1 formula

For raw damage `raw > 0` and defense `D ≥ 0`:

`D_l1 = clamp(raw - D, 0.1 * raw, 0.9 * raw)`

`raw ≤ 0` yields `D_l1 = 0`.

Physical hits use `D = physical` (stat core only). Each elemental line uses `D = core(T)` as above.

**Pierce (physical subtype, strict override):** if the weapon’s dominant physical type is Pierce, physical **raw** going into Layer 1 is multiplied by **1.3** when the **victim** is in any of: `Actor::IsAttacking()`, `ActorState::IsSprinting()`, `ActorState` stagger flag set, or knock state other than `kNormal` (opening / hit reaction windows — see `OverrideDamageHook.cpp`).

Combined strict-override HP from the Req pipeline is a sum over one physical line (vanilla melee/projectile raw treated as that line today) plus each non-zero elemental line from `ExtractHitSourceFromWeaponAndSpell`.

### 2.2 Layer 2 (Multiplicative Mitigation)

For each component after Layer 1:

`D_l2 = D_l1 * Π_i (1 - N_i)`

Each `N_i` is an **absorbed fraction** in `[0, 1]` (stored internally as decimal, e.g. 10% → `0.1`). Values are clamped to `[0, 1]` when applied. Sources are merged from:

- Worn armor **instance enchantments** and **active effects** on the target with `ERCF.MGEF.Absorption` plus an `ERCF.DamageType.*` keyword (`EspRouting`: `absorptionFractions[type]`).

**Physical (strict override):** the game’s hit supplies one physical raw amount; the plugin picks a **dominant physical subtype** from the weapon (`ResolveDominantPhysicalSubtype` / `WEAPON_TYPE` fallback). Layer 2 uses `absorptionFractions[subtype]` for that subtype only (Standard / Strike / Slash / Pierce).

**Elemental:** each elemental type uses its own `absorptionFractions[type]`.

**Contextual modifiers** (after the product, still in the override combiner):

- **Silver vs undead:** if the victim has `ActorTypeUndead` and the weapon has `WeaponMaterialSilver`, physical `D_l2` is multiplied by **1.2** (`Layer2SilverVsUndeadPhysicalScalar`).
- **Sun (Dawnguard):** if the hit spell or weapon enchant has `DLC1SunDamage` or `DLC1SunDamageUndead` on any magic effect, the **Magic** elemental line’s `D_l2` is multiplied by **0** when the target is not undead, else **1** (`Layer2SunMagicScalar`).

**After Σ D_l2:** the total is multiplied by `Proc::GetDamageTakenMultiplier` (e.g. frostbite +incoming damage), then clamped to non-negative.

`DamageHP = max(0, (Σ D_l2) * takenMultiplier)`

## 3) Worked Example (Level 75, strict-override inputs, **Req fallback**)

Same numeric story as the **non-ERAS** branch of §2.1, using `Req_ComputeDefenseBucketsL1` (rounded). With ERAS, substitute the six `DefenseSheet` values instead of these derived numbers.

Input (victim):

- level: 75
- maxHP: 720 (as fed into `Req_ComputeDefenseBucketsL1`)
- maxMP: 130
- armorRating: `100` (example: ERAS `defense.physical` or `kDamageResist` fallback)

Derived:

- `BaseDefense = 10 + (75 + 78) / 2.483 ≈ 71.62`
- level increment: `(71 - 1) * 0.40 + (75 - 71) * 1.00 = 28 + 4 = 32`
- extra physical from HP: `0.1 * (720 / 10) = 7.2`
- extra physical from armor: `0.1 * 100 = 10`
- extra elemental from MP: `0.1 * (130 / 10) = 1.3`

**L1 buckets**

- `physical ≈ 71.62 + 32 + 7.2 + 10 = 120.82`
- each of `elemental[Magic] … elemental[Lightning] ≈ 71.62 + 32 + 1.3 = 104.92`

Layer-2 setup for the example (absorption only):

- slash: `10%` → `N = 0.1`
- fire: `20%` → `N = 0.2`

Cases (ignore silver/sun/frostbite mult for simplicity):

1. **120** physical raw, treated as slash-dominant:
   - L1: `clamp(120 - 120.82, 12, 108) = 12`
   - L2: `12 * (1 - 0.1) = 10.8`

2. **300** physical raw, slash-dominant:
   - L1: `clamp(300 - 120.82, 30, 270) = 179.18`
   - L2: `179.18 * (1 - 0.1) ≈ 161.26`

3. Split **150** physical (slash) + **150** fire (elemental line from enchant/spell):
   - L1 physical: `clamp(150 - 120.82, 15, 135) = 29.18`
   - L1 fire: `clamp(150 - 104.92, 15, 135) = 45.08`
   - L2 total: `29.18 * 0.9 + 45.08 * 0.8 ≈ 62.33`

## 4) Status Effect Damage

Status buildup pops are threshold-based. Threshold families:

- `Robustness`
- `Immunity`
- `Focus`
- `Madness` (custom bucket)

Status pop damage is not reduced by Layer 1/Layer 2.

### 4.1 Bleed

- Resistance family: `Robustness`
- Robustness basis: max HP
- Robustness cap by proc count:
  - 1st proc: cap `300`
  - 2nd proc: cap `450`
  - 3rd+ proc: cap `600`
- Armor/jewelry enchantments can increase resistance (cap applies after bonuses).
- Damage:
  - player + standard NPC: `10% weapon physical damage + 15% max HP`
  - boss: `10% weapon physical damage + 7.5% max HP`

### 4.2 Frostbite

- Resistance family: `Robustness` (same threshold model as bleed)
- Damage:
  - player + standard NPC: `5% weapon damage + 10% max HP`
  - boss: `5% weapon damage + 7% max HP`
- Secondary effects:
  - target takes `+20%` damage from all sources for `30s`
  - reduced stamina recovery for `30s`
  - during buildup, incoming fire damage decreases frostbite buildup
  - on frostbite pop, frostbite status is instantly cleared

### 4.3 Madness

- Resistance family: `Madness`
- Threshold basis: `(max HP + max MP) * 0.5`
- Armor/jewelry enchantments can increase resistance.
- Effect (player only):
  - `10% max HP + 20` flat HP damage
  - instant drain `10% max MP + 30` flat MP

### 4.4 Poison

- Resistance family: `Immunity`
- Threshold basis: `(max HP + max SP) * 0.5`
- Armor/jewelry enchantments can increase resistance.
- DOT: `0.07% max HP + 7` flat per second for `90s`
- DOT ticks bypass Layer 1/Layer 2.

### 4.5 Rot

- Resistance family: `Immunity`
- Threshold basis: `(max HP + max SP) * 0.5`
- Armor/jewelry enchantments can increase resistance.
- DOT: `0.18% max HP + 10` flat per second for `90s`
- DOT ticks bypass Layer 1/Layer 2.

### 4.6 Sleep

- Resistance family: `Focus`
- Threshold basis: max MP
- Armor/jewelry enchantments can increase resistance.
- Damage: `0`
- player + standard NPC:
  - drain all MP
  - paralyze `5s` (via MGEF)
- boss:
  - disabled

## 5) Stamina Damage

Each blocked attack deals stamina damage instead of HP in this path.

### 5.1 Base stamina damage

`BaseStaminaDamage = 10 + (weaponWeight * 1.2)`

### 5.2 Motion value multiplier

- light attack: `1.0`
- power attack: `1.5`
- sprint attack: `1.5`
- sprint power attack: `2.0`

`IncomingStaminaDamage = BaseStaminaDamage * MotionValueMultiplier`

Example:

- greatsword weight `15`, light attack:
  - `10 + (15 * 1.2) = 28`

### 5.3 Block reduction (guard point)

- guard point source:
  - shield user: shield armor value
  - no shield: defending weapon stamina damage model

Final stamina damage:

`StaminaDamageReceived = max(0, IncomingStaminaDamage - GuardPoint)`

## 6) Magicka Damage

To be decided.
