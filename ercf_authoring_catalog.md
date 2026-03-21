# ERCF Authoring Catalog (v1)

This is a consolidated reference for the **author-authored** content you need to tag in your ESP.
It lists the canonical **keywords**, **MGEF category keywords**, the **MGEF naming templates**, and the **spell/perk templates** defined by the contract.

Runtime implementation note:

- The current DLL runtime extracts/applies **Poison** and **Bleed** procs only.
- The current DLL runtime reads resist bands **Immunity** and **Robustness** only.
- The contract below includes additional statuses/bands, but they are not yet consumed by the runtime.

## 1) ERCF Keyword Set (exact strings)

### Damage type keywords (component routing)

- `ERCF.DamageType.Phys.Standard`
- `ERCF.DamageType.Phys.Strike`
- `ERCF.DamageType.Phys.Slash`
- `ERCF.DamageType.Phys.Pierce`
- `ERCF.DamageType.Elem.Magic`
- `ERCF.DamageType.Elem.Fire`
- `ERCF.DamageType.Elem.Lightning`
- `ERCF.DamageType.Elem.Holy`

### Resistance-band keywords

- `ERCF.ResBand.Immunity`
- `ERCF.ResBand.Robustness`
- `ERCF.ResBand.Focus`
- `ERCF.ResBand.Vitality`

### Status keywords (proc family ids)

- `ERCF.Status.Poison`
- `ERCF.Status.ScarletRot`
- `ERCF.Status.Bleed`
- `ERCF.Status.Frostbite`
- `ERCF.Status.Sleep`
- `ERCF.Status.Madness`
- `ERCF.Status.DeathBlight`

### ESP keyword template (general form)

Use this form when you need to route authored status application to a specific proc id:

- `ERCF.<Category>.<Id>`

## 2) MGEF Category Keywords (what the runtime looks for)

Attach these category keywords to the relevant MGEFs so the plugin can route magnitudes:

- `ERCF.MGEF.Defense`
- `ERCF.MGEF.Absorption`
- `ERCF.MGEF.Buildup`
- `ERCF.MGEF.ResBand`

## 3) MGEF Naming Templates (recommended author-facing names)

These are naming templates only; the runtime routing is keyword-driven.

### 3.1 Defense (Layer 1)

- Name template: `ERCF_MGEF_Defense.<DamageType>.<T>`
- Must include keywords:
  - `ERCF.MGEF.Defense`
  - `ERCF.DamageType...<T>`
- Magnitude meaning: `Defense_T` contribution (flat number into the Defense curve)

### 3.2 Absorption (Layer 2)

- Name template: `ERCF_MGEF_Absorption.<DamageType>.<T>`
- Must include keywords:
  - `ERCF.MGEF.Absorption`
  - `ERCF.DamageType...<T>`
- Magnitude meaning:
  - interpreted as `abs = clamp01(magnitude / 100)` producing an “absorbed fraction”

### 3.3 Status buildup payload

- Name template: `ERCF_MGEF_Buildup.<StatusId>.<S>`
- Must include keywords:
  - `ERCF.MGEF.Buildup`
  - `ERCF.Status...<S>`
- Magnitude meaning: the MGEF magnitude is converted into a meter payload fraction of the landed weapon damage.
  - If magnitude is `<= 1.0`, it is treated as a fraction (e.g. `0.25` = 25%)
  - If magnitude is `> 1.0`, it is treated as a percent (e.g. `25` = 25%)
  - Raw meter payload = `weaponPhysicalDamage * fraction`

### 3.4 Status resistance (band value)

- Name template: `ERCF_MGEF_ResBand.<Band>.<B>`
- Must include keywords:
  - `ERCF.MGEF.ResBand`
  - `ERCF.ResBand...<B>`
- Magnitude meaning: `ResValue_band` input to the effective payload divisor
- Runtime source note:
  - ERCF reads resistance from **active effects currently on the target actor** (not by scanning equipment records directly).
  - So the NPC/player must have an active effect instance whose base MGEF is tagged with `ERCF.MGEF.ResBand` + `ERCF.ResBand.<Band>`.
  - Equipment/perks/passives should apply these MGEFs to the actor as active effects.

## 4) Spell Templates (if you want spell-authored routing)

The runtime design allows spells to carry the same routing keywords as components (or to act as a VFX carrier).
The contract defines these spell templates:

- Component carrier (optional)
  - Name template: `ERCF_Spell_ComponentCarrier.<DamageTypes...>`
  - Purpose: apply the correct `ERCF.DamageType...` keywords and weights to the outgoing damage instance
- Status proc visuals (optional)
  - Name template: `ERCF_Spell_ProcVisual.<StatusId>`
  - Purpose: apply Skyrim-side VFX only; the numeric proc application/event emission stays in the DLL

## 5) Perk Templates (if you need conditional mechanics)

- Puncture counter bypass
  - Name template: `ERCF_PERK_PierceCounterBypass`
  - Purpose: during the target’s attack animation window, modifies piercing negation behavior
  - Magnitude meaning: bypass value `bypass` (exact math is TBD by the implementation)

