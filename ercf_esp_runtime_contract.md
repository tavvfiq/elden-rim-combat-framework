# ERCF ESP Runtime Contract (Reference)

This document defines the **authoring contract** between ESP (keywords/MGEFs/spells/perks) and the SKSE plugin runtime math.

It exists to answer, for any authored Skyrim Form:

- “What does this represent in ERCF?”
- “Which combat formula consumes it?”
- “What parameters come from which magnitude/keyword slot?”

This is written for v1. Later expansions should preserve these contracts or version them explicitly.

---

## 1) Canonical enums (runtime meaning)

### Damage types

Damage components are always tagged with one of these runtime types:

**Physical subtypes**
- `Standard`
- `Strike`
- `Slash`
- `Pierce`

**Elements**
- `Magic`
- `Fire`
- `Lightning`
- `Holy`

> Rule: a single component is tagged with exactly **one** damage type. Mixed hits are represented as multiple components (split).

### Status effects (proc families)

Status effects always belong to one of the four resistance bands:

| Resistance band | Afflictions (v1) |
| :--- | :--- |
| `Immunity` | `Poison`, `ScarletRot` |
| `Robustness` | `Bleed` (Exsanguination), `Frostbite` (Stalhrim Chill) |
| `Focus` | `Sleep` (Vaermina’s Pall), `Madness` (Sheogorath’s Touch) |
| `Vitality` | `DeathBlight` (Soul Tear) |

> Rule: resistances gate **buildup/proc**, not raw HP damage.

---

## 2) Damage pipeline formulas (HP damage)

All formulas run **per damage component** and then sum the results.

Let:
- `ATK_T` = attacker-provided attack value for component damage type `T`
- `Defense_T` = target flat defense for component type `T` (Layer 1 input)
- `AbsProd_T` = multiplicative remaining damage factor after Absorption (Layer 2 output input)
- `k_defense` = global tuning constant for the Defense curve (set in plugin config / not authored per-form)

### 2.1 Layer 1 — Flat Defense (threshold-ish, non-linear)

The goal is: weak attacks are heavily penalized, but high attacks punch through.

**Defense definition:** `Defense_T` is the total target defense for component type `T`:

`Defense_T = DefenseBase_T(level, baseHealth) + Σ gear/contribution terms for type T`

Where:
- `DefenseBase_T(level, baseHealth)` is the dynamic part scaling with the target’s base level and base health
- `gear/contribution terms` are sums from authored mitigation MGEFs / armor traits / temporary buffs

The doc leaves the exact base curve implementation to the plugin config, but the runtime math below consumes `Defense_T` as a single flat number.

Use:

`PostDefense_T = ATK_T * ATK_T / (ATK_T + k_defense * Defense_T)`

Notes:
- If `Defense_T = 0`, then `PostDefense_T = ATK_T`
- If `ATK_T << k_defense * Defense_T`, then `PostDefense_T` becomes very small
- This curve is intentionally monotonic and easy to tune via `k_defense`

### 2.2 Layer 2 — Absorption (multiplicative %)

Absorption comes from multiple sources (armor material, traits, buffs).

Represent each absorption source as `abs_i,T` where:
- `abs_i,T ∈ [0, 1)` is “absorbed fraction”

Compute:

`AbsProd_T = Π_i (1 - abs_i,T)`

Final component HP damage:

`Damage_T = PostDefense_T * AbsProd_T`

**Immunity rule:** to prevent “100% immunity forever”, the plugin clamps the product:

`Damage_T = max(Damage_T, DamageFloor_T)`

Common defaults:
- `DamageFloor_T = max(ATK_T * 0.001, 0.5)` (example)
- A separate boss/phase flag may override floors when you want explicit immunity.

### 2.3 Split damage model

If a hit is authored as multiple components, split is represented explicitly as:

`ATK_T = ATK_total * w_T`

Where:
- `w_T ≥ 0`
- `Σ_T w_T = 1`

Then:

`TotalDamage = Σ_T Damage_T`

> Rule: each component performs Layer 1 + Layer 2 independently. There is no “sum then mitigate”.

---

## 3) Status buildup + proc formulas

Status is modeled as a per-target per-status **buildup meter**.

Let, for a particular status id `S`:
- `Meter_S` in range `[0, 100]`
- `Payload_S` from the attacking source (MGEF magnitude, spell payload, etc.)
- `ResBand(R)` = target resistance band value (Immunity/Robustness/Focus/Vitality)
- `k_resist` = global tuning constant

### 3.1 Effective payload (resistance ↔ buildup interaction)

The simplest v1 contract:

`EffectivePayload_S = Payload_S / (1 + k_resist * ResValue_band)`

Where `ResValue_band ≥ 0`.

Interpretation:
- Higher resistance → slower meter fill (Elden-style pacing)
- No hard binary “immune unless exactly threshold” unless explicitly scripted

### 3.2 Meter accumulation

On a successful qualifying hit (or tick, depending on the affliction definition):

`Meter_S = min(100, Meter_S + EffectivePayload_S)`

**Pop rule:**
- If `Meter_S >= 100` then: pop occurs
- v1 pop: `Meter_S` resets to `0` (no multi-pop chaining per single hit)

### 3.3 Decay loop (pressure vs passive defense)

Maintain:
- `LastBuildupTime_S` timestamp per status meter
- `DecayDelay` (seconds) — time after last buildup where meter is held
- `DecayRate` — meters decay when decay is active

For runtime ticks over `dt`:

If `now - LastBuildupTime_S < DecayDelay`:
- `Meter_S` unchanged

Else:
- `Meter_S = max(0, Meter_S - DecayRate * dt)` (linear; `DecayRate` is buildup points per second)

### 3.4 Proc application and event emission

On pop:
1. Plugin applies the status proc effect (HP damage over time, stagger, debuffs, etc.)
2. Plugin emits a **public proc event** (so other DLL mods can react)
3. Plugin resets meter `Meter_S = 0`

**Proc damage rule:** any direct HP damage caused by the proc should, by default, run through the same HP pipeline (Defense → Absorption) unless you explicitly mark it as “true damage / bypass”.

---

## 4) ESP authoring contract (Keywords, MGEFs, Spells, Perks)

This section is the “what to put in the ESP” guidance and how SKSE interprets it.

### 4.1 Keyword conventions

Keywords are used for **typing and routing** (selecting a runtime id / stat bucket).

Recommended naming format:
`ERCF.<Category>.<Id>`

Examples (v1):
- `ERCF.DamageType.Phys.Standard`
- `ERCF.DamageType.Phys.Strike`
- `ERCF.DamageType.Phys.Slash`
- `ERCF.DamageType.Phys.Pierce`
- `ERCF.DamageType.Elem.Magic`
- `ERCF.DamageType.Elem.Fire`
- `ERCF.DamageType.Elem.Lightning`
- `ERCF.DamageType.Elem.Holy`

Resistance-band routing:
- `ERCF.ResBand.Immunity`
- `ERCF.ResBand.Robustness`
- `ERCF.ResBand.Focus`
- `ERCF.ResBand.Vitality`

MGEF category routing (optional but recommended):
- `ERCF.MGEF.Defense`
- `ERCF.MGEF.Absorption`
- `ERCF.MGEF.Buildup`
- `ERCF.MGEF.ElementalDamage` (weapon/spell enchant: magnitude is treated as **elemental attack** on hit and runs through Defense→Absorption; **do not** pair with a vanilla DamageHealth effect unless you intend double application)
- `ERCF.MGEF.ResBand`

Status ids:
- `ERCF.Status.Poison`
- `ERCF.Status.ScarletRot`
- `ERCF.Status.Bleed`
- `ERCF.Status.Frostbite`
- `ERCF.Status.Sleep`
- `ERCF.Status.Madness`
- `ERCF.Status.DeathBlight`

### 4.1.1 Recommended v1 form templates (names + meaning)

Use these as naming templates so it is obvious to both you and future contributors what each form feeds into.

#### MGEFs — numeric contributions

1) **Defense (Layer 1)**
- Name: `ERCF_MGEF_Defense.<DamageType>.<T>`
- Keywords on the MGEF: `ERCF.MGEF.Defense` + `ERCF.DamageType...<T>`
- Magnitude meaning: `Defense_T` contribution (a flat number in the plugin’s Defense curve units)

2) **Absorption (Layer 2)**
- Name: `ERCF_MGEF_Absorption.<DamageType>.<T>`
- Keywords on the MGEF: `ERCF.MGEF.Absorption` + `ERCF.DamageType...<T>`
- Magnitude meaning: absorption percent for that type, interpreted as:
  - `abs = clamp01(magnitude / 100)`

3) **Status buildup payload**
- Name: `ERCF_MGEF_Buildup.<StatusId>.<S>`
- Keywords on the MGEF: `ERCF.MGEF.Buildup` + `ERCF.Status...<S>`
- Magnitude meaning: `Payload_S` per qualifying hit/tick

4) **Status resistance (band value)**
- Name: `ERCF_MGEF_ResBand.<Band>.<B>`
- Keywords on the MGEF: `ERCF.MGEF.ResBand` + `ERCF.ResBand...<B>`
- Magnitude meaning: `ResValue_band` (unitless scaling input to the resistance payload divisor)

#### Spells / Perks — routing and behavior hooks

5) **Component carrier (optional)**
- Name: `ERCF_Spell_ComponentCarrier.<DamageTypes...>`
- Purpose: apply the correct damage type keywords and weights to the outgoing damage instance (especially for spells that are not directly weapon-derived).
- If you skip this, you can instead attach the damage keywords directly to the spell itself.

6) **Puncture counter bypass (content rule)**
- Name: `ERCF_PERK_PierceCounterBypass`
- Purpose: during the target’s attack animation window, modifies negation behavior for piercing hits.
- Magnitude meaning: bypass value `bypass` where the plugin effectively reduces required negation for that window (exact math TBD as a plugin config; keep it as “fraction of negation negated” or “effective absorption” depending on your implementation).

7) **Status proc visuals (optional)**
- Name: `ERCF_Spell_ProcVisual.<StatusId>`
- Purpose: apply Skyrim-side VFX only; the plugin remains the source of numeric proc application and event emission.

### 4.2 Damage components from ESP (weapon/spell typing)

For an incoming hit, the plugin builds a list of **damage components**:

`[{ type: T, weight: w_T, payloadOrATKSource: ... }, ...]`

**Authoring inputs (v1 approach):**
- The attacking weapon/spell carries one or more **damage type keywords** from section 4.1.
- If multiple damage type keywords are present on the source, plugin expects equal weights by default unless you provide explicit weights via:
  - a dedicated “component weight” MGEF, or
  - a configured mapping table in plugin that reads magnitudes from a known “carrier” MGEF.

> Rule: if you want deterministic split ratios, you must provide them as authored data (not equal weighting assumptions).

### 4.3 Defense / Absorption contributions from ESP (target side)

**Current plugin behavior (v1+ hit pipeline):**

- **Physical (Standard / Strike / Slash / Pierce):** Layer-1 defense is derived from **worn armor pieces** only:
  - Each `TESObjectARMO` contributes `GetArmorRating() * armor_rating_defense_scale` (see `ercf.toml`) split across the four physical types using `ERCF.DamageType.Phys.*` keywords on that armor (equal split if none).
- **Elemental (Magic / Fire / Lightning / Holy):** Layer-1 defense and Layer-2 absorption are read only from **enchantments on worn items** (weapon enchants on equipped weapons do not apply to the target):
  - Enchant MGEFs tagged `ERCF.MGEF.Defense` + `ERCF.DamageType.Elem.*` add elemental flat defense.
  - Enchant MGEFs tagged `ERCF.MGEF.Absorption` + `ERCF.DamageType.Elem.*` add elemental absorption (magnitude ÷ 100, clamped).

**Active effects (after worn armor):** `MergeMitigationFromActiveActorEffects` / `MergeStatusResistanceFromActiveActorEffects` add, on top of worn-armor results:

- `ERCF.MGEF.Defense` + `ERCF.DamageType.*` (physical **or** elemental): magnitude adds to flat `Defense_T` for that type.
- `ERCF.MGEF.Absorption` + type: magnitude ÷ 100 as an absorption layer for that type.
- `ERCF.MGEF.ResBand` + band keyword: same as enchant rules for buildup pacing.

Use this for **creature skin** (race spells, constant abilities on dragons, etc.) when they have no meaningful worn `ARMO`.

> **Pipeline note:** Skyrim’s default weapon HP damage does not yet run through ERCF’s per-type indices. Extra ERCF elemental hit damage and status procs **do** use `defense` / `takenMult` for their mapped types (e.g. Bleed → Standard, Poison → Magic). **Slash-specific** defense on a dragon therefore applies once you route **slash-typed** attack damage through ERCF (future hook) or you approximate with `ERCF.MGEF.TakenMult` + Slash if that index is applied to the damage in question.

### 4.3.1 Status resistance sourcing (target side)

- **Immunity / Robustness** band values for buildup pacing are read from **enchantments on worn items** only (`ERCF.MGEF.ResBand` + band keyword + magnitude).

### 4.3.2 Damage-type matchup (taken multiplier, after Defense→Absorption)

ERCF applies an extra **per-damage-type multiplier** to HP damage **after** the Defense→Absorption pipeline:

1. **Armor-weight blend (BOD2):** Each worn `TESObjectARMO` contributes `GetArmorRating()` to a **heavy**, **light**, or **clothing** bucket from `BGSBipedObjectForm::GetArmorType()`.  
   For each damage type `T`, the base taken multiplier is a **rating-weighted average** of three configurable rows (`matchup_heavy_*`, `matchup_light_*`, `matchup_clothing_*` in `ercf.toml`).  
   If total worn armor rating is **0** (naked), the **clothing** row is used for all types (“flesh / unarmored” baseline).

2. **Per-actor overrides:** Active effects on the target with **`ERCF.MGEF.TakenMult`** + **`ERCF.DamageType...`** multiply that type further; **magnitude** is treated as a direct multiplier (e.g. `0.2` on Holy = takes 20% Holy damage after armor blend). Use for bosses, undead, stone/crystal races, etc.

3. **Clamp:** Result is clamped to `[matchup_taken_mult_min, matchup_taken_mult_max]`.

> **Content note:** Strike-vs-stone / Holy-vs-undead / Fire-vs-plants are best expressed with **`ERCF.MGEF.TakenMult`** (or similar) on races, abilities, or boss phases; the three armor rows only know **heavy / light / clothing**, not material species.

### 4.4 Status buildup contributions from ESP (attacker side)

Status buildup payloads are sourced from the **instance enchantment on the attacking weapon** (`GetAttackingWeapon()->GetEnchantment()` effect list) and/or **`HitData::attackDataSpell`** (when present), not from arbitrary active effects on the attacker.

Contract:
- The MGEF is tagged with `ERCF.MGEF.Buildup` + `ERCF.Status.<StatusId>`.
- The MGEF **magnitude** is interpreted as the `Payload_S` for that status id.
- When the hit lands and qualifies as “successful status delivery”, the plugin increments the meter.

Where qualification comes from:
- Hit type matches (melee vs ranged vs spell)
- The damage instance is part of an authored ERCF damage component
- Optional: timing window / cooldown / proc-per-hit cap

### 4.5 Status proc definitions (what happens on pop)

Each status id has a corresponding proc handler in the plugin.

Example “v1 behavior mapping”:
- `Poison`: apply periodic HP drain
- `ScarletRot`: aggressive periodic HP drain
- `Bleed`: apply immediate % max HP chunk
- `Frostbite`: deal burst damage + apply a temporary stamina regen debuff + (optional) amplifies incoming physical damage by applying an absorption modifier MGEF
- `Sleep`: stagger/paralyze + open crit window content hook
- `Madness`: zero magicka + psychic burst
- `DeathBlight`: execute-style effect with strict boss rules (immunity or threshold)

ESP content for these:
- If you need visuals or actor effects, spells/perks can apply Skyrim-native VFX/debuffs.
- If you need exact tuning, the plugin applies the numeric effect and optionally mirrors it with MGEF for visuals.

### 4.6 Special mechanics (example: Puncture counter + “shell shatter”)

These are typically authored as perks or conditional MGEFs:

**Puncturing counter-attack bypass (content rule)**
- Implement as a perk that, during the target’s attack animation window, sets:
  - “incoming piercing damage during that window bypasses X% of negation”
- Represent the X% bypass value as either:
  - a perk magnitude, or
  - a configured constant keyed by `ERCF.DamageType.Phys.Pierce`

**State-based shell shatter**
- Implement as a temporary lowering of `Defense_T` or `abs_i,T` after repeated poise/heavy impacts.
- In v1, prefer a “shell HP” system:
  - each qualifying hit reduces a shell durability
  - when it hits zero, apply a temporary actor modifier MGEF that lowers defense/absorption for remainder/phase

---

## 5) Public event surface (for other DLL mods)

When a meter pops for status `S`, emit an event.

Event contract (v1):
- `eventType`: `ERCF.StatusProc`
- `attackerRef` (actor ref/handle)
- `targetRef`
- `statusId` (`S`)
- `band` (Immunity/Robustness/Focus/Vitality)
- `meterValueBeforePop` (optional, useful for tuning)
- `payloadSnapshot` (optional)
- `magnitudeSnapshot` (optional)
- `gameTime`

Other mods can subscribe and react (play custom VFX, spawn particles, apply extra logic).

---

## 6) Minimal v1 authoring checklist (to reduce mistakes)

To add one damage type + one status to v1, you should create:

1. One `ERCF.DamageType...` keyword for the weapon/spell
2. One attacker status MGEF tagged `ERCF.Status.<StatusId>` with correct payload magnitude
3. One target mitigation MGEF/keyword that changes either:
   - `Defense_T`, or
   - `Absorption_T`
4. (Optional) one resistance band MGEF that increases:
   - `ResBand.Immunity` / `ResBand.Robustness` / `ResBand.Focus` / `ResBand.Vitality`
5. Confirm the pop emits the expected `ERCF.StatusProc` event and the plugin applies the proc effect

