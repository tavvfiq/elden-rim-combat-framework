# Elden Rim Combat Framework — Core Concept

**Basis:** FromSoftware-style combat math as in *Elden Ring*: typed damage, per-type mitigation, split damage, and status buildup gated by resistances. Implementation target is Skyrim; mechanics below are engine-agnostic.

---

## 1. Design Pillars (what we are building)

| Pillar | Intent |
| :--- | :--- |
| **Typed damage** | Every hit carries one or more **components**, each tagged with exactly one **damage type** (physical subtype or element). |
| **Typed mitigation** | Each component is reduced by defenses that match **that type** — not by one global “armor” number. |
| **Status (proc) combat** | Afflictions use **buildup meters**; **status resistances** change how fast meters fill and how fast they decay. |
| **Two layers + split** | Each damage component passes **Defense (flat)** then **Absorption (%)**; multi-type hits are **split** and evaluated per type. |

---

## 2. Damage Types (ER-aligned)

### 2.1 Physical subtypes

Same family as *Elden Ring*: **Standard**, **Strike**, **Slash**, **Pierce**.

- **Standard** — default when nothing else applies; neutral interactions.
- **Strike** — blunt; strong vs rigid / plate / stone; often slower.
- **Slash** — edges; strong vs flesh, cloth, light armor; weak vs hard plate / heavy mail.
- **Pierce** — thrust / narrow point; strong vs scales, thick hide, gaps; positional bonuses can stack here as *content*, not as core math.

### 2.2 Elements

Aligned with ER: **Magic**, **Fire**, **Lightning**, **Holy**.

- Elements use the **same two-layer pipeline** as physical subtypes.
- **Arcane / “pure magicka”** can be mapped to **Magic** unless you need a separate type later; fewer types keeps tuning tractable.

---

## 3. Two-Layer Mitigation + Split Damage

### 3.1 Pipeline (per damage component)

For each component of type \(T\) with attack value \(ATK_T\):

1. **Layer 1 — Defense (flat)**  
   Subtract the target’s **Defense\(_T\)** (or a grouped defense that applies to \(T\)) from \(ATK_T\) using a **single non-linear curve** shared by all types (so small hits are soaked, large hits retain impact).  
   Output: **post-defense damage** \(D_T\).

2. **Layer 2 — Absorption (multiplicative %)**  
   Apply **Absorption\(_T\)** for that type (from armor, traits, buffs). Multiple sources combine **multiplicatively**, e.g.  
   \(\text{final} = D_T \times \prod_i (1 - \text{abs}_i)\), with a **floor** so 100% immunity never occurs unless explicitly scripted (e.g. boss phase).

**Order is fixed:** Defense first, then Absorption — same idea as ER’s “flat reduction then negation.”

### 3.2 Split damage

If an attack deals \(ATK_{\text{Slash}} + ATK_{\text{Fire}}\):

- The **Slash** portion uses Defense and Absorption for Slash (and any “physical” grouping you define).
- The **Fire** portion uses Defense and Absorption for Fire independently.

**Why it matters:** Mixed weapons pay **two flat checks** and **two absorption checks**. Pure builds are easier to optimize; split is viable when base numbers or mechanics (DoT, chip, poise) compensate — same tradeoff as ER.

---

## 4. Resistances — Damage vs Status (two different systems)

*Elden Ring* separates **damage mitigation** (Defense / Absorption per damage type) from **status resistances** (Immunity, Robustness, Focus, Vitality). This doc keeps that separation explicit.

### 4.1 Mitigation stats (incoming HP damage)

- **Per-type Defense** — flat layer for each physical subtype and each element you support (or grouped: one “physical” defense + four elemental defenses if you need to reduce scope).
- **Per-type Absorption** — % layer from gear, ashes/wax equivalents, spells, stance.

**Interaction:** Damage type \(T\) only interacts with **Defense\(_T\)** and **Absorption\(_T\)**. A fireball does not use Slash defense; a greataxe does not use Lightning absorption.

Optional **content** layer (not core math): weak surfaces, stances, or enemy parts can **modify** effective Defense\(_T\) / Absorption\(_T\) for that hit.

### 4.2 Status resistances (proc pacing)

Aligned with ER’s four resistance bands:

| Resistance | Typical afflictions | Role |
| :--- | :--- | :--- |
| **Immunity** | Poison, Scarlet Rot (or your “blight”) | Slows **Immunity** meter fill; may speed decay |
| **Robustness** | Hemorrhage (Bleed), Frostbite | Same for **Robustness** meter |
| **Focus** | Sleep, Madness | Same for **Focus** meter |
| **Vitality** | Death Blight (if used) | Same for **Vitality** meter |

**Interaction with proccing:**

- Each status source has **buildup** per hit (and optionally per tick).
- Effective buildup is reduced by the target’s resistance (ER: higher resistance → bar fills slower).
- When the meter reaches **full**, proc fires, meter **resets** (and often gains temporary immunity to that status — optional but ER-like).

**Decay:** If the target avoids gaining that status buildup for a window, the meter **decays** — rewards pressure; punishes passive play.

**No double-dip:** Status resistances gate **proc**, not raw weapon damage, unless you explicitly design a skill that ties them (keep rare).

---

## 5. Status Effects — Minimal ER-style loop

1. **Define a small set for v1** (e.g. Poison, Bleed, Frostbite, one mental effect) before adding every affinity.
2. Each affliction: **on proc** effect + **optional** ongoing effect; **Death-type** effects need **boss rules** (immunity or capped).
3. **Buildup** scales from weapon/spell; **resistance** scales from gear, attributes, and buffs.

---

## 6. Content Rules (optional, later)

- **Anatomy / weak points:** Local multipliers on Defense\(_T\) or Absorption\(_T\) — does not replace the global pipeline.
- **Armor shatter / stagger:** State that **lowers** Defense or Absorption for a phase — same two-layer model, different numbers.
- **Environment:** Fire / Lightning modifiers when wet — apply **symmetrically** to player and enemies where possible.

---

## 7. Vertical Slice (recommended first milestone)

Prove the loop before breadth:

1. **Physical:** Standard + one subtype (e.g. Slash vs Strike) with different Absorption on two armor sets.
2. **Elemental:** One element (e.g. Fire) with its own Defense/Absorption.
3. **Split:** One mixed weapon; verify it underperforms on heavily defended targets unless tuned.
4. **Status:** One meter (e.g. **Robustness** + Bleed) with decay and one resistance stat tuning knob.

---

## 8. Why this is “better” than a single flat document

- **ER mechanics are the spine:** types → per-type layers → split; status is **parallel** with its own resistances.
- **Fewer special-case rules** in core math; flavor (Dwemer vents, rain) attaches to **modifiers**, not alternate formulas.
- **Clear implementation order:** pipeline first, then statuses, then anatomy and env.

This is the concept set you described: **damage types, resistances that interact with those types, status procs with their own resistances, and two-layer defense with split damage** — explicitly modeled after *Elden Ring*, portable to Skyrim as a framework.
