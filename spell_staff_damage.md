# ERCF Spell Scaling Formulas

## Overview
This document defines the specific mathematical formulas used by `ERCF` to scale vanilla Skyrim magic effects (MGEFs) based on the player's attributes and equipped catalysts.

---

## 1) Variable Definitions

* **`VanillaMag`**: The base magnitude of the Magic Effect defined in the Creation Kit (e.g., 40).
* **`CatalystBase`**: The base power assigned to the equipped Staff/Spell (e.g., 12.0).
* **`ScaleCoef`**: The attribute scaling letter grade converted to a decimal (e.g., B-Scaling = 0.90).
* **`PlayerStat`**: The player's current custom attribute value (`ER_INT` or `ER_FTH`).
* **`StatSat`**: The resulting decimal multiplier (0.0 to 1.0) after running the player's attribute through the diminishing returns curve.

---

## 2) The Stat Saturation Curve (Lerp)

the formula is the same with weapon attribute scaling

---

## 3) The Catalyst Formula (Equipped Staff/Seal)

If the `CastingSource` is a **staff**, the missile multiplier is anchored to the **same hand baseline** as §4, then applies a **small tier premium** (better staff / tier → slightly higher) and an optional **staff-enchant** term from `ERCF.WeaponAttributeScaling.INT` / `.FTH` (percent magnitudes → decimal `ScaleCoef`).

**Tier score:** `CatalystBase` is still resolved from material keywords, spell skill tier, or staff gold value, then **clamped** to `[spell_catalyst_base_min, spell_catalyst_base_max]` in `ercf.toml`.

`Tier01 = saturate((CatalystBase - min) / (max - min))` (or `0.5` if `min == max`).

**Step 1 — same baseline as hands (using the staff’s primary stat saturation, INT or FTH)**  
`HandMult = 1.0 + (InnateCoef * StatSat)`  (same `spell_innate_coef` as §4).

**Step 2 — tier premium (staff only slightly above hands at default tuning)**  
`Premium = lerp(spell_staff_tier_premium_min, spell_staff_tier_premium_max, Tier01)`

**Step 3 — staff enchant scaling (kept mild via `spell_staff_enchant_k`)**  
`Enchant = 1.0 + (spell_staff_enchant_k * ScaleCoef * StatSat)`

**Final**  
`FinalMagnitude = VanillaMag * HandMult * Premium * Enchant`

Legacy (pre-balance) formula `CatalystBase + CatalystBase * ScaleCoef * StatSat` is removed from the staff missile path; `spell_catalyst_base_*` now mainly drives **which tier** you are on the premium lerp, not a raw ×6–12 multiplier.

---

## 4) The Bare-Hands Formula (Innate / Fallback)

If the `CastingSource` is the player's bare hands, bypass the catalyst base and apply a much smaller multiplier to penalize non-catalyst casting. 

*(Note: `InnateCoef` is a global mod setting, e.g., 1.5, to determine how strong bare-handed magic can be).*

**The Formula:**
`FinalMagnitude = VanillaMag * (1.0 + (InnateCoef * StatSat))`

### 4.1 Hand spells vs Layer 1 / Layer 2 (low “~2” final damage)

Missile **elemental raw** is `VanillaMag × §4 factor × elemental_enchant_damage_scale`, then **attribute bonus** `floor(base × coeff × saturation)` is added **before** L1. Hand casts usually have **no `HitData.weapon`**, so there is **no enchant** → those coeffs were **0**, while ERAS **L1** and **L2** still applied. That leaves a small number that collapses to chip damage.

**`spell_hand_intrinsic_attr_coef`** in `ercf.toml` adds a synthetic scaling line on the **higher of INT vs FTH** when there is **no weapon enchant** (same coeff units as `weapon_attribute_scaling.md`). Staff shots still use only the staff’s `formEnchanting` (no double intrinsic).

---

## 5) ERCF strict override (projectiles)

When `override_mode_strict` is on and a missile has **no** physical estimate (`weaponDamage` / bow fallback ~0) but carries an `rd.spell` with **ERCF.MGEF.ElementalDamage** on the spell (and/or staff `weaponSource` template), ERCF runs the requirement pipeline on **elemental only** with:

- **VanillaMag** (per type): summed MGEF magnitudes as today, times `elemental_enchant_damage_scale`, then multiplied by a single **spell missile factor** from §3 or §4:
  - **Staff** (`TESObjectWEAP::IsStaff()`): `SpellStaffMissileMultiplier(...)` per §3 — tier score from **`WeapMaterial*`** / spell **`GetMinimumSkillLevel()`** max / staff **`GetGoldValue()`** (same resolution as before), clamped to `[spell_catalyst_base_min, spell_catalyst_base_max]`. `scaleCoef` from staff enchant `ERCF.WeaponAttributeScaling.INT` or `.FTH` (percent → decimal), else `spell_fallback_scale_coef`; `StatSat` from matching ERAS attribute.
  - **Otherwise** (hands / non-staff): `SpellBareHandsScalingMultiplier(spell_innate_coef, StatSat)` with **INT** saturation.

Toml: `spell_catalyst_base_min`, `spell_catalyst_base_max`, `spell_innate_coef`, `spell_hand_intrinsic_attr_coef`, `spell_fallback_scale_coef`, `spell_staff_tier_premium_min`, `spell_staff_tier_premium_max`, `spell_staff_enchant_k`.

Weapon attribute scaling (STR/DEX/…) is still applied **after** that factor, on each elemental base, inside `ComputeRequirementDamageForVictim`.
