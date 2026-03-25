# Override Strategy Design (ERCF Owns Final HP Damage)

This document defines how ERCF will fully own final HP damage per hit (override strategy), instead of stacking on top of vanilla damage.

## 1) Objective

For qualifying combat hits:

- Vanilla HP damage is neutralized for that hit.
- ERCF computes final HP damage from the requirement model.
- ERCF applies exactly one HP result to the target.

Result: one authoritative damage pipeline, no double-application.

## 2) High-Level Model

Per qualifying hit:

1. Capture hit context at a stable runtime hook.
2. Build ERCF component model (physical subtype + elemental components).
3. Run Layer 1 + Layer 2 + status interactions.
4. Compute `finalHPDamage`.
5. Prevent vanilla HP damage from being applied (or neutralize before commit).
6. Apply `finalHPDamage` via ERCF.

## 3) Ownership Rules

## 3.1 ERCF-Owned HP Paths

ERCF owns and applies:

- weapon/spell direct HP impact for qualifying hits
- split physical/elemental routing
- all layer-1/layer-2 reductions
- status pop/burst outcomes
- poison/rot DOT tick damage

## 3.2 Vanilla-Owned Paths (initially out of override scope unless explicitly added)

- environmental hazards not mapped into ERCF
- scripted/quest HP manipulation that does not pass through the hit path
- debug console direct AV writes

Note: Each out-of-scope path remains vanilla until explicitly onboarded.

## 4) Hook and Execution Architecture

## 4.1 Hook Requirement

The hook point must expose:

- attacker and target actor
- raw hit data and source context
- a way to suppress or zero vanilla HP damage for that hit safely

## 4.2 Candidate Pattern

Use a deterministic “pre-commit damage” hook:

- Intercept damage resolution before target HP is finalized.
- Read source and hit metadata.
- Zero/suppress vanilla HP contribution for this hit.
- Apply ERCF computed HP once.

If exact pre-commit suppression is unavailable on a runtime version:

- fallback to controlled correction mode is allowed only in debug branch, never final shipping mode.

## 4.3 Re-entrancy Guard

When ERCF calls `DamageActorValue`, it must not trigger its own override path recursively.

Implement:

- thread-local re-entry flag or hit-token guard
- guard exits immediately for internal ERCF applications

## 5) Qualifying Hit Rules

A hit is override-eligible when:

- attacker and target are valid actors
- target is alive
- hit is not blocked (if block path is modeled separately)
- source type is one of supported paths (melee/ranged/spell per phase scope)

Non-eligible hits:

- pass through vanilla unchanged (until onboarded)

## 6) Damage Computation in Override Mode

Use the requirement model exactly:

- Layer 1:
  - physical consolidated bucket
  - elemental per-type bucket
  - per-component clamp `0.1*raw .. 0.9*raw`
- Layer 2:
  - physical subtype-specific mitigation buckets
  - elemental type-specific mitigation
  - race elemental resistance as relevant type mitigation source
- sum component outputs to final HP

Status logic:

- threshold/pop by resistance family
- burst/pops bypass layers as specified
- poison/rot DOT ticks bypass layers and tick first at `t=1s`

## 7) Vanilla Neutralization Strategy

Preferred neutralization order:

1. **True suppression**: stop vanilla HP damage contribution at hook point.
2. **Hard zero route**: set per-hit HP term to zero before engine commit.
3. **Correction fallback (non-final)**: if neither is possible on a branch/runtime, do not ship as override mode.

Shipping requirement:

- override mode is enabled only if suppression is deterministic and stable.

## 8) Conflict and Compatibility Rules

Other mods may also alter damage.

Policy:

- ERCF override mode should be explicit and opt-in config.
- Add a compatibility mode toggle:
  - `override_mode = strict|off`
- In `strict`, ERCF expects ownership of qualifying HP hits.
- In `off`, ERCF can revert to additive behavior for compatibility testing.

## 9) Telemetry and Debugging

For each overridden hit (debug mode):

- log hit id, attacker, target
- log vanilla raw estimate and suppressed value
- log ERCF component breakdown
- log final applied HP
- log status meter changes and proc emission

Invariant checks:

- exactly one HP application per qualifying hit
- no recursive self-trigger
- no negative/NaN final damage

## 10) Failure Safety

If hook integrity fails at runtime:

- automatically disable strict override mode
- emit warning once (rate-limited)
- fall back to vanilla pass-through (or additive debug mode if enabled)

Never leave partial suppression active without ERCF apply.

## 11) Rollout Phases

### Phase A: Instrumentation

- Add internal hit IDs and deterministic logs
- verify hook visibility and metadata integrity across sample fights

### Phase B: Suppression Proof

- prove vanilla HP term can be suppressed deterministically
- validate no recursion and no duplicate applies

### Phase C: ERCF Final Apply

- apply full requirement pipeline under strict override
- validate parity against spreadsheet/test vectors

### Phase D: Edge Coverage

- ranged, spell, power, sprint attacks
- creatures, bosses, race resistance
- status pop + DOT timing

### Phase E: Compatibility Gate

- test with common combat mods
- document known incompatibilities and config guidance

## 12) Acceptance Criteria

Override strategy is considered complete when:

- qualifying hits produce one final HP outcome from ERCF only
- no vanilla double-application remains on covered paths
- deterministic behavior across supported AE/SE runtime targets
- status and stamina requirements pass manual validation checklist

## 13) Open Technical Item

- Exact hook symbol(s) and Address Library IDs are runtime/version-specific and must be finalized in the implementation phase.

