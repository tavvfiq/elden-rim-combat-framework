# Damage Model Requirement

## 1) HP Damage Types

There are two top-level HP damage categories:

- **Physical**: `Standard`, `Strike`, `Slash`, `Pierce`
- **Elemental**: `Fire`, `Frost`, `Poison`, `Lightning`, `Magic` (generic)

Design intent:

- Standard: neutral vs most armor types
- Strike: strong vs heavy armor and hard-skin creatures
- Slash: strong vs flesh, cloth, and light armor
- Pierce: good armor penetration

## 2) Two-Layer HP Processing

HP damage always passes through two layers.

### 2.1 Layer 1 (Flat Defense)

Layer 1 is global for player and NPC and is based on:

- level
- attributes (health, magicka)
- armor rating

#### Base defense

`BaseDefense = 10 + (level + 78) / 2.483`

#### Additional level increment (also applied)

- level 1..71: `+0.40` defense per level
- level 72..91: `+1.00` defense per level
- level 92..160: `+0.21` defense per level
- level 161..713: `+0.036` defense per level

#### Attribute and armor contributions

- Physical defense: `+0.1` per 10 health
- Magical/elemental defense: `+0.1` per 10 magicka
- Physical defense: `+0.1` per armor rating

#### Layer 1 defense buckets

- Physical subtypes (`Standard/Strike/Slash/Pierce`) are consolidated into one **physical** defense bucket in Layer 1.
- Elemental uses per-type defense (`Fire/Frost/Poison/Lightning/Magic`).

#### Layer 1 formula

For each damage component:

`D_l1(component) = clamp(raw(component) - defense(component), 0.1 * raw(component), 0.9 * raw(component))`

Total Layer 1 output:

`D_l1_total = Σ D_l1(component)`

### 2.2 Layer 2 (Multiplicative Mitigation)

For each component:

`D_l2(component) = D_l1(component) * (1 - N1) * (1 - N2) * (1 - N3) * ...`

Where `N` is a mitigation percentage in decimal form from armor, amulet, ring, perk, race, etc.

Important:

- Layer 2 for physical is subtype-specific:
  - Slash damage uses slash mitigation bucket
  - Strike damage uses strike mitigation bucket
  - Pierce damage uses pierce mitigation bucket
  - Standard damage uses standard mitigation bucket
- Race resistances apply in Layer 2 for relevant elemental type.
- Contextual type modifiers in Layer 2:
  - Silver material bonus applies to all physical subtypes when target is undead.
    - Source keyword: `WeaponMaterialSilver` (vanilla)
    - Target keyword: `ActorTypeUndead` (vanilla)
  - Sun damage is treated as a separate split component using `Magic` type.
    - Source tags: `DLC1SunDamage` (general) or `DLC1SunDamageUndead`
    - Non-undead target: apply 100% reduction in layer 2 (`* 0`)
    - Undead target: no special reduction (`* 1`)


Total HP damage after both layers:

`DamageHP = Σ D_l2(component)`

## 3) Worked Example (Level 75 Nord)

Input:

- level: 75
- HP: 720
- SP: 100
- MP: 130
- armor rating: 100

Derived defenses:

- base defense: `10 + (75 + 78)/2.483 = 85.03`
- defense from level increment: `(0.4 * 70) + (1 * 3) = 31`
- extra physical from HP: `72 * 0.1 = 7.2`
- extra physical from armor rating: `100 * 0.1 = 10`
- extra elemental from MP: `13 * 0.1 = 1.2`

Final defense:

- physical: `85 + 31 + 7.2 + 10 = 133.2`
- fire: `85 + 31 + 1.2 = 117.2`
- frost: `85 + 31 + 1.2 = 117.2`
- poison: `85 + 31 + 1.2 = 117.2`
- lightning: `85 + 31 + 1.2 = 117.2`
- magical: `85 + 31 + 1.2 = 117.2`

Layer-2 mitigation setup for example:

- slash mitigation: `10%`
- fire mitigation: `20%`

Cases:

1. `120` slash raw:
   - Layer 1: `clamp(120 - 133.2, 12, 108) = 12`
   - Layer 2: `12 * (1 - 0.1) = 10.8`

2. `300` slash raw:
   - Layer 1: `clamp(300 - 133.2, 30, 270) = 166.8`
   - Layer 2: `166.8 * (1 - 0.1) = 150.12`

3. Split hit `150` slash + `150` fire:
   - Layer 1 slash: `150 - 133.2 = 16.8`
   - Layer 1 fire: `150 - 117.2 = 32.8`
   - Layer 2 total: `(16.8 * 0.9) + (32.8 * 0.8) = 41.36`

Final damage values: `10.8`, `150.12`, `41.36`

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
