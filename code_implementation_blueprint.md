# Code Implementation Blueprint

This document explains how the current requirement will be implemented in code.
It is a coding blueprint, not an implementation diff.

## 1) Goals

- Implement the HP two-layer model exactly as specified:
  - Layer 1: flat defense with clamp
  - Layer 2: multiplicative mitigation, bucket-specific
- Keep physical handling split by phase:
  - Layer 1 physical consolidated
  - Layer 2 physical subtype-specific (`Standard/Strike/Slash/Pierce`)
- Implement status thresholds and proc effects with boss branching and bypass policy.
- Implement block stamina damage path with guard point and clamp.
- Preserve compatibility with ESP-driven authoring (keywords + MGEFs + enchantments).

## 2) Target Files and Responsibilities

### `src/HitEventHandler.cpp`

Primary runtime orchestrator per qualifying hit.

Will be responsible for:

- Reading hit context (`TESHitEvent`, `HitData`, attacker/target).
- Resolving outgoing damage components from source weapon/spell.
- Running HP pipeline (Layer 1 -> Layer 2) per component.
- Applying resulting HP damage.
- Applying status buildup changes and proc checks.
- Emitting status proc events.
- Updating Prisma UI meters.
- Optional debug logging (already present via config flag).

### `src/EspRouting.h/.cpp`

Data extraction and routing layer.

Will be responsible for:

- Resolving damage type tags and source magnitudes.
- Building mitigation buckets from worn armor + enchantments + active effects.
- Providing per-type layer-2 mitigation vectors.
- Providing status threshold contributions from enchantments/active effects.
- Providing boss detection by keyword (placeholder now).

### `src/CombatMath.h`

Pure math utilities.

Will contain:

- Layer 1 clamp function for component damage.
- Layer 2 multiplicative mitigation function.
- Helper for combined HP result from components.
- Status threshold and buildup helpers.
- DOT tick scheduling helpers (time-based utility only).
- Stamina block formula helpers.

### `src/Proc.h/.cpp`

Status effect application executor.

Will be responsible for:

- Applying burst proc outcomes (bleed/frostbite/madness/sleep).
- Applying DOT setup/runtime for poison/rot.
- Enforcing bypass policy for status damage.
- Applying boss scaling branch via boss keyword.
- Publishing or consuming messaging payloads as needed.

### `src/Messaging.h/.cpp`

Event contract and dispatch.

Will be extended for:

- Distinct status proc payload fields required by new status logic.
- Optional extra messages for DOT lifecycle (start/stop/tick) if needed.

### `src/Config.h/.cpp` and `ercf.toml.example`

Tunables and defaults.

Will include:

- Layer 1 constants and clamps.
- Layer 2 min/max safety clamps if required.
- Status constants (durations, percentages, caps, tick intervals).
- Stamina damage constants.
- Boss keyword placeholder string.

## 3) HP Pipeline Implementation

## 3.1 Damage Component Model

At runtime each hit will be represented as component entries:

- Physical components: one subtype (`Standard/Strike/Slash/Pierce`) with raw value.
- Elemental components: zero or more (`Fire/Frost/Poison/Lightning`) with raw values.

Notes:

- Layer 1 uses consolidated physical defense bucket.
- Layer 2 uses subtype-specific physical mitigation bucket.

## 3.2 Layer 1 (Flat Defense)

For each component:

1. Resolve defense bucket:
   - Physical subtype -> `DefensePhysicalL1`
   - Elemental subtype -> `DefenseElemL1[type]`
2. Compute:
   - `postL1 = clamp(raw - defense, 0.1*raw, 0.9*raw)`

Defense construction:

- `BaseDefense(level) = 10 + (level + 78)/2.483`
- plus additive piecewise level increment term
- plus attribute terms (health for physical, magicka for elemental)
- plus armor rating term for physical only

## 3.3 Layer 2 (Mitigation Product)

Per component:

- `final = postL1 * product(1 - N_i_for_this_component_type)`

Routing:

- Slash component reads only slash mitigation sources.
- Strike reads strike.
- Pierce reads pierce.
- Standard reads standard.
- Fire/Frost/Poison/Lightning each read matching elemental mitigation.

Race resistance:

- Mapped as type-specific layer-2 mitigation source for relevant elemental type.

## 3.4 Final HP Application

- Sum finalized components and apply once per hit to target HP.
- CommonLibSSE-NG application API:
  - use `target->AsActorValueOwner()->ModActorValue(RE::ActorValue::kHealth, -finalHPDamage)`
  - convention: damage is a negative delta
- Keep per-component debug logs to verify routing and bucket use.

## 4) Status System Implementation

## 4.1 Threshold Families

Per target maintain status meters + thresholds:

- Robustness -> Bleed/Frostbite
- Immunity -> Poison/Rot
- Focus -> Sleep
- Madness bucket -> Madness

Threshold formulas:

- Robustness: based on max HP, with proc-count cap progression:
  - 1st pop cap 300
  - 2nd pop cap 450
  - 3rd+ cap 600
  - cap applied after enchant bonuses
- Immunity: `(maxHP + maxSP) * 0.5` + bonuses
- Focus: `maxMP` + bonuses
- Madness: `(maxHP + maxMP) * 0.5` + bonuses

## 4.2 Buildup and Pop

- On qualifying hit, add buildup payload by status type.
- Compare meter vs threshold.
- On pop, apply status outcome and reset/advance proc counters per status.

## 4.3 Status Outcomes

### Bleed (bypass L1/L2)

- Player/standard NPC: `10% weapon physical damage + 15% maxHP`
- Boss: `10% weapon physical damage + 7.5% maxHP`

### Frostbite (bypass L1/L2 for burst)

- Player/standard NPC: `5% weapon damage + 10% maxHP`
- Boss: `5% weapon damage + 7% maxHP`
- Apply 30s debuffs:
  - `+20% damage taken`
  - reduced stamina recovery
- Fire interaction:
  - during buildup, fire decreases frostbite buildup
  - on pop, frostbite status clears immediately

### Madness (bypass L1/L2, player-only)

- HP burst: `10% maxHP + 20`
- MP drain: `10% maxMP + 30`

### Poison DOT (bypass L1/L2)

- Tick: `0.07% maxHP + 7`
- Duration: 90s
- First tick at `t=1s`

### Rot DOT (bypass L1/L2)

- Tick: `0.18% maxHP + 10`
- Duration: 90s
- First tick at `t=1s`

### Sleep

- Player/standard NPC only:
  - damage 0
  - drain all MP
  - paralyze 5s (via MGEF/effect application path)
- Boss: disabled

## 4.4 Boss Branching

- Boss identified by keyword placeholder: `ERCF.Actor.Boss`
- One function in routing layer should centralize this check.

## 5) Stamina Damage on Block

Implemented in dedicated helper path:

1. `base = 10 + weaponWeight * 1.2`
2. apply motion multiplier:
   - light `1.0`, power `1.5`, sprint `1.5`, sprint power `2.0`
3. `incoming = base * motionMult`
4. compute guard point:
   - shield user -> shield armor value
   - no shield -> defending weapon stamina-damage model
5. `received = max(0, incoming - guardPoint)`
6. apply to target stamina

## 6) Data/Authoring Contract Mapping

Authoring expectations in ESP:

- Damage type keywords on source content.
- Layer-2 mitigation by subtype/type through enchantments/perks/race effects.
- Status buildup payloads and resistance modifiers through enchant effects.
- Boss keyword assignment to actors/NPC templates.

Runtime responsibilities:

- Resolve and normalize authored data.
- Avoid mixing unrelated buckets.
- Ensure every mitigation source is type-scoped before multiplication.

## 7) Rollout Plan (Code Phases)

### Phase 1: Math + Data Structures

- Add/adjust pure math functions in `CombatMath.h`.
- Define status state structs and threshold/proc counters.
- Add config keys/defaults for all new constants.

### Phase 2: HP Pipeline

- Refactor `HitEventHandler` to compute component model.
- Apply Layer 1 and Layer 2 exactly per spec.
- Add debug logs for per-component bucket routing.

### Phase 3: Status Runtime

- Implement threshold buildup/pop and proc-count caps.
- Implement each status outcome path in `Proc.cpp`.
- Implement DOT scheduler/state for poison/rot.

### Phase 4: Stamina Block Damage

- Add block stamina path with formula and guard-point rules.
- Add clamp and logs.

### Phase 5: Validation

- Add deterministic test vectors for formulas.
- Add in-game checklist cases:
  - physical subtype routing
  - race elemental resistance in layer 2
  - robust/immunity/focus/madness thresholds
  - boss vs non-boss behavior
  - DOT timing (first tick at 1s)

## 8) Out of Scope (Current)

- Magicka damage model (still TBD).
- UI redesign beyond existing meter surfaces.
- Content pack authoring itself (this blueprint is runtime-side).
