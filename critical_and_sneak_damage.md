# ERCF Critical & Sneak Damage Formulas

## critical

critical damage = (base weapon + scaling) * criticalmultiplier  * motion_value

this critical damage going into layer 1 and 2

motion_value:
Skyrim Keyword,Critical Motion Value (MV)
WeapTypeDagger,4.20
WeapTypeSword / Katana,3.45
WeapTypeGreatsword,2.75
WeapTypeWarhammer,2.63
WeapTypeMace / WarAxe,3.03
WeapTypeBow,1.00 (No critical animations)

## sneak

sneak deal 120% damage as raw damage before going to layer 1 and 2

---

## Implementation (ERCF strict melee / shared `ComputeRequirementDamageForVictim`)

- Physical raw prefers `HitData::physicalDamage` when > 0; otherwise the caller-supplied fallback (melee captures `totalDamage` before it is cleared).
- Sneak: `kSneakAttack` → multiply that raw by **1.2** before weapon-attribute scaling.
- Critical: `kCritical` and not `kIgnoreCritical` → after **(sneak-adjusted raw + attribute bonus)**, multiply by `HitData::criticalDamageMult` × **motion value** from weapon keywords (`WeapTypeDagger`, `WeapTypeSword` / `WeapTypeKatana`, `WeapTypeGreatsword`, `WeapTypeWarhammer`, `WeapTypeMace` / `WeapTypeWarAxe`, bow/crossbow **1.0**; default **3.45**). If `criticalDamageMult` is ~0 at pre-commit (common), treat it as **1.0** so damage is not zeroed; motion value still applies.
- Then pierce vulnerability (+30% physical raw), then L1 / L2 as usual.