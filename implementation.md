# Implementation Plan — Elden Rim Combat Framework

Stack: **ESP** (authoritative data) + **SKSE C++ plugin** (runtime pipeline, events) + **PrismaUI** (HUD, native API) + **CommonLibSSE-NG**. Concept: [`concept.md`](concept.md). ESP contract: [`ercf_esp_runtime_contract.md`](ercf_esp_runtime_contract.md).

---

## Decisions (locked)

| Topic | Choice |
| :--- | :--- |
| **Source of truth** | **ESP.** All tunable combat data (damage typing, absorption/defense knobs, buildup payloads, proc definitions as data) lives in forms the plugin reads. C++ applies the shared formulas only; it does not hardcode balance numbers that contradict the ESP. |
| **Build** | **xmake** (not CMake) for the native plugin. |
| **UI** | **PrismaUI via C++ only** — request `PrismaUI.dll` API, create views, push state (see reference mod below). |
| **Testing** | **Yes** — at least unit tests for pure math (defense curve, split breakdown, buildup/decay thresholds). |
| **RTK** | For **Cursor agent** terminal use when fetching context — see [`.cursor/rules/rtk-cli.mdc`](.cursor/rules/rtk-cli.mdc). Not a requirement for human dev workflows. |

---

## Repository layout (suggested)

| Piece | Role |
| :--- | :--- |
| `plugin/` | SKSE plugin — **xmake** + CommonLibSSE-NG |
| `esp/` or `Data/` | `.esp` / `.esl` source (xEdit, CK); binaries optional in git |
| `ui/` | PrismaUI-facing HTML/assets if not embedded |

---

## 1. ESP — source of truth

**Owns:** keywords, magnitudes, archetype MGEFs, spells/perks that define *what* applies and *how strong* it is.

- **Keywords** — weapons, armors, races, hazards; the plugin resolves these to typed coefficients.
- **Magic effects / spells / perks** — buildup rates, caps, resist links, proc payloads **as authored values** the runtime reads.

**Principle:** Rebalance by editing the ESP (and regenerating if you use a compiler pipeline); the DLL stays the **engine**, not the **spreadsheet**.

---

## 2. SKSE plugin — hit hook, damage, buildup, events

**Pipeline (conceptual):**

1. **Hook the hit / damage path** — one place where incoming or outgoing hits are visible with attacker, target, weapon, and magnitudes (exact RE hooks TBD when targeting a Skyrim build).
2. **Damage** — read ESP-driven typing and mitigation; apply two-layer **Defense → Absorption** per component; handle **split** damage.
3. **Status buildup** — on the same hit, add buildup from authored payloads; apply resistances; decay when idle; when a meter **pops**, **emit a framework event** (proc fired).

**Proc damage:** the core plugin **listens** to its own proc event to apply the proc’s damage / secondary effects (so proc logic is not scattered). Same event is **public** so other DLLs can subscribe.

**Extensibility for other mods:** document a stable **event surface** (payload: target, attacker, status id, magnitude snapshot, etc.). Implementation options to choose when coding: SKSE **Messaging** interface broadcasts, a small **registrar** exported from your DLL, or both (message for loose coupling, registrar for zero-latency C++). Third-party mods listen and react when a buildup **pops**.

**Principle:** No duplicated combat math in Papyrus; optional Papyrus is only for legacy or non-performance-critical glue if ever needed.

---

## 3. UI (PrismaUI, C++)

**Reference implementation:** `/Users/taufiq.nugroho/Documents/Projects/skyrim/souls-style-loot` — see `xmake.lua`, `src/PrismaUI.cpp`, `src/PrismaUI_API.h` (modder-facing API header), `RequestPluginAPI` on `PrismaUI.dll`, `CreateView` / `InteropCall` / listeners as needed.

**PrismaUI project (local):** `/Users/taufiq.nugroho/Documents/Projects/skyrim/PrismaUI`

**Principle:** HUD reflects plugin state over the native API; **no** Papyrus bridge required for UI.

---

## 4. CommonLibSSE-NG

**Path (local):** `/Users/taufiq.nugroho/Documents/Projects/skyrim/CommonLibSSE-NG`

- Consume via **xmake** package rule (git submodule, local path, or fetch — match how `souls-style-loot` or your template pins CLNG).
- Pin **Skyrim / Address Library** version in docs and CI.

---

## 5. Build & tooling

- **xmake** + **MSVC** on Windows for the SKSE DLL; document minimum VS and Windows SDK.
- **RTK:** agent-only; see rule file.

### xmake baseline (from `souls-style-loot`)

Use `/Users/taufiq.nugroho/Documents/Projects/skyrim/souls-style-loot/xmake.lua` as the starting template and keep these conventions unless we have a reason to deviate:

- `set_xmakever("2.8.2")`
- `set_languages("c++23")`
- `add_rules("mode.debug", "mode.releasedbg")` and `set_defaultmode("releasedbg")`
- `includes("lib/CommonLibSSE-NG")`
- target depends on `commonlibsse-ng` and uses `add_rules("commonlibsse-ng.plugin", { ... })`
- `add_files("src/**.cpp")`, `add_headerfiles("src/**.h")`, `add_includedirs("src")`
- `set_pcxxheader("src/pch.h")`

This gives us the same build shape as the known working project, then we swap plugin metadata and source paths for ERCF.

---

## 6. Testing

- **Unit tests** for pure functions: defense curve, per-type absorption composition, split allocation, buildup increment + decay + threshold, proc ordering.
- **Integration** remains manual in-game; keep a short checklist in `docs/` when you add hooks.

---

## 7. Vertical slice (implementation order)

1. Plugin loads, logs version, CLNG linked (xmake).
2. Read one keyword set from ESP → map to damage types for one weapon.
3. Hit hook: two-layer mitigation + one split weapon path.
4. One status meter + resistance + decay; **emit** proc event; internal listener applies proc damage.
5. PrismaUI: one meter / debug overlay via C++ API (souls-style-loot pattern).
6. Document **public proc event** for external mods.

---

## 8. Open decisions (fill in when locking binaries)

- Minimum Skyrim AE/SE build and Address Library version.
- Exact **RE hook** symbols for hit/damage (per game version).
- **Event wire format** (Messaging-only vs exported API + Messaging).

---

## 9. Manual Verification Checklist (v1)

This checklist is designed to validate the current v1 vertical slice: hit interception → buildup meters → proc pop event → internal proc damage via Defense→Absorption → PrismaUI HUD.

### A. Setup (ESP authoring)

Ensure the test attacker provides at least:

- Poison buildup payload (weapon enchant MGEF):
  - This repo currently sources status buildup from the attacker's **weapon enchantment effects** on hit.
  - MGEF keywords: `ERCF.MGEF.Buildup` + `ERCF.Status.Poison`
  - magnitude semantics:
    - if `magnitude <= 1.0`, treated as a fraction (e.g. `0.25` = 25%)
    - if `magnitude > 1.0`, treated as a percent (e.g. `25` = 25%)
  - raw meter payload is computed as: `weaponPhysicalDamage * fraction`
- Bleed buildup payload (weapon enchant MGEF):
  - This repo currently sources status buildup from the attacker's **weapon enchantment effects** on hit.
  - MGEF keywords: `ERCF.MGEF.Buildup` + `ERCF.Status.Bleed`
  - magnitude semantics:
    - if `magnitude <= 1.0`, treated as a fraction (e.g. `0.25` = 25%)
    - if `magnitude > 1.0`, treated as a percent (e.g. `25` = 25%)
  - raw meter payload is computed as: `weaponPhysicalDamage * fraction`

Ensure the test target (player) provides resistance and mitigation:

- Immunity resistance band:
  - MGEF keywords: `ERCF.MGEF.ResBand` + `ERCF.ResBand.Immunity`
  - magnitude = `ResValue_band` (v1 pacing divisor)
- Robustness resistance band:
  - MGEF keywords: `ERCF.MGEF.ResBand` + `ERCF.ResBand.Robustness`
  - magnitude = `ResValue_band`
- Runtime note:
  - ERCF sums resistance from **active effects currently on the target actor** (not by directly reading equipment records).
  - ERCF also caches summed resistances per actor for `status_resist_cache_ttl_seconds` (see `ercf.toml`), so if you toggle gear/passives in-game, resistance may take up to ~TTL seconds to change.
- Magic mitigation (so Poison proc damage hits the pipeline):
  - MGEF keywords: `ERCF.MGEF.Defense` + `ERCF.DamageType.Elem.Magic` (+ optional absorption keywords `ERCF.MGEF.Absorption`)
- Standard mitigation (so Bleed proc damage hits the pipeline):
  - MGEF keywords: `ERCF.MGEF.Defense` + `ERCF.DamageType.Phys.Standard` (+ optional absorption keywords `ERCF.MGEF.Absorption`)

### B. Logs to watch

Open `Data/SKSE/Plugins/ERCF/` logs and look for:

- Meter updates:
  - `ERCF: Player meters poison=<...> bleed=<...>`
- Proc pops (event received by the internal listener):
  - `ERCF: StatusProc received (attacker=... target=... status=... band=... meter=... payload=...)`

Health changes should match the Defense→Absorption pipeline (poison maps to `Magic`, bleed maps to `Standard` in current v1).

### C. In-game tests

1. Successful hit accumulates buildup
   - Have the player (target) receive hits from an attacker that provides Poison/Bleed payloads.
   - Confirm the HUD Poison/Bleed bars rise from 0.
   - Confirm the meter log lines update each hit.

2. Meter pop triggers proc
   - Keep landing hits until Poison and/or Bleed reaches 100.
   - Confirm a `StatusProc received` log line appears.
   - Confirm player HP decreases on pop.
   - Confirm the corresponding meter resets to 0 in the next HUD update.

3. Blocked hit does not accumulate
   - Block/parry an incoming hit that would normally apply buildup.
   - Confirm no meter increase on blocked events (TESHitEvent `kHitBlocked` is skipped).

4. Decay hold + decay phase
   - Stop applying buildup and wait longer than `status_decay_delay_seconds`.
   - Trigger another hit (to force a UI refresh).
   - Confirm the meter value is reduced (decayed) before accumulating new buildup.

