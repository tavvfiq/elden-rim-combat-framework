# Elden Ring: Weapon Attribute Scaling Calculation

Attribute scaling is entirely percentage-based. The bonus damage you get from your stats is directly tethered to the weapon's base damage.

Here is the exact mathematical formula the game engine uses to calculate that "+X" bonus damage number on your stat screen.

---

## 1. The Core Scaling Formula

For every damage type a weapon has, the game runs this specific equation:

*Bonus Damage = floor(Base Damage * Scaling Multiplier * Stat Saturation)*

(Note: The "floor" function means the game calculates the decimals and then rounds completely down to the nearest whole number).

---

## 2. The Variables (Terms)

Here is exactly what each of those three hidden variables means:

### Base Damage
This is the raw, hardcoded damage of the weapon before any stats are applied. Because scaling is a percentage of base damage, upgrading your weapon at Master Hewg mathematically forces your bonus damage to increase as well, even if you never level up your stats.

### Scaling Multiplier (The Letters)
The letter grades (S, A, B, C, D, E) are UI representations of a hidden decimal multiplier. In **ERCF**, you author the MGEF **magnitude as a percent of base damage** (Creation Kit number), not the decimal:
* *S-Tier:* **140** or higher (140% → multiplier 1.40)
* *A-Tier:* **101–139**
* *B-Tier:* **75–100**
* *C-Tier:* **45–74**
* *D-Tier:* **25–44**
* *E-Tier:* **1–24**

ERCF converts **magnitude ÷ 100** before `floor(base × multiplier × saturation)`.

### Stat Saturation (The Soft Caps)
You do not get the full multiplier just by meeting the minimum requirements. The game uses a curved percentage (from 0% to 100%) based on your actual stat level (from 1 to 99).
* *Levels 10-20:* You access roughly 20% of the weapon's scaling potential.
* *Levels 50-60:* You access roughly 75% of the potential.
* *Level 80:* You access about 90% of the potential. (Leveling from 80 to 99 requires millions of Runes and only gives you the remaining 10%).

---

## 3. Putting It Together (Concrete Example)

Let's say you are using a fully upgraded Heavy Nightrider Glaive (+25). You have *80 Strength*, which is the optimal soft cap.

* *Base Damage:* The weapon has a hardcoded base physical damage of 245.
* *Scaling Multiplier:* It has an "S" in Strength; you’d set enchant magnitude **160** (160% → 1.60 in the formula).
* *Stat Saturation:* Because you are at 80 Strength, the curve grants you roughly 90% (or 0.90) of that multiplier. 

The game calculates your bonus damage like this:

1. Bonus Damage = floor(245 * 1.60 * 0.90)
2. Bonus Damage = floor(392 * 0.90)
3. Bonus Damage = floor(352.8)
4. Bonus Damage = 352

On your status screen, your weapon's Attack Power will display as *245 + 352*. Your total physical damage entering the enemy's defense calculation is 597.

---

## 4. ERCF implementation (strict damage override)

When **strict override** is on (`override_mode_strict` in `ercf.toml`), ERCF adds weapon attribute scaling using the same formula for **physical** and for each **elemental** line from `ERCF.MGEF.ElementalDamage` (weapon enchant + hit spell, after `elemental_enchant_damage_scale`):

`bonus = Σ floor(base × magnitude × saturation(stat))`

- **Physical base damage:** `a_rawVanillaDamage` for that hit (melee or projectile), *before* the pierce vulnerability multiplier.
- **Elemental base damage:** per-type MGEF sum × `elemental_enchant_damage_scale` from `ercf.toml`; the attribute bonus is computed **per element type** from that type’s base (same enchant coefficients and attacker stats as physical).
- **Magnitude (percent):** sum of effect magnitudes on the weapon’s **base enchantment** (`TESObjectWEAP::formEnchanting` / object template enchant), from MGEFs that carry the keywords below. Values are **percent of base** (e.g. **90** = 90%, **160** = 160%). Multiple effects stack by **adding percents** per stat line, then **÷ 100** for the internal multiplier. 
- **Stat levels:** attacker’s ERAS attributes (`str`, `dex`, `intl`, `fth`, `arc`) via `getStatsSnapshotForActor` / player snapshot. If ERAS is unavailable or the aggressor cannot be resolved, **no** attribute bonus is applied.

### CK / xEdit keywords (per MGEF)

Each scaling MGEF should carry **one** stat keyword (FormID examples from plugin data):

| Stat | Keyword |
|------|---------|
| STR | `ERCF.WeaponAttributeScaling.STR` |
| DEX | `ERCF.WeaponAttributeScaling.DEX` |
| INT | `ERCF.WeaponAttributeScaling.INT` |
| FTH | `ERCF.WeaponAttributeScaling.FTH` |
| ARC | `ERCF.WeaponAttributeScaling.ARC` |

Set the effect **magnitude** on the enchantment to the **percent** (e.g. **160** for a strong STR line → 1.60× in the bonus formula). The soft-cap curve is approximated in code as a piecewise linear saturation on stat level **1–99** (aligned with §2: ~20% by level 20, ~75% by mid-50s, ~90% at 80, 100% at 99).

**Note:** Instance-only enchantments on the live `InventoryEntryData` are not read here yet; only the weapon record’s `formEnchanting` path used by `HitData::weapon` is scanned.