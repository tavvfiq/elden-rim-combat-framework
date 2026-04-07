#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "CombatMath.h"
#include "Config.h"

#include <RE/A/Actor.h>
#include <RE/E/EffectSetting.h>
#include <RE/H/HitData.h>
#include <RE/I/InventoryEntryData.h>
#include <RE/M/MagicItem.h>
#include <RE/T/TESObjectWEAP.h>

namespace ERCF
{
	namespace Esp
	{
		// Elemental IDs match requirement.md §1 (Fire / Frost / Poison / Lightning / Magic).
		enum class DamageTypeId : std::uint32_t
		{
			Standard = 0,
			Strike = 1,
			Slash = 2,
			Pierce = 3,
			Magic = 4,
			Fire = 5,
			Frost = 6,
			Poison = 7,
			Lightning = 8,
			kTotal
		};

		constexpr std::size_t kDamageTypeCount = static_cast<std::size_t>(DamageTypeId::kTotal);

		struct MitigationCoefficients
		{
			std::array<float, kDamageTypeCount> defense{};
			// Multiplicative layer-2 absorption sources, stored as absorbed fractions.
			std::array<std::vector<float>, kDamageTypeCount> absorptionFractions{};
		};

		struct StatusResistanceCoefficients
		{
			float immunityResValue = 0.0f;
			float robustnessResValue = 0.0f;
			float focusResValue = 0.0f;
			float madnessResValue = 0.0f;
		};

		struct StatusBuildupCoefficients
		{
			float poisonPayload = 0.0f;
			float bleedPayload = 0.0f;
			float rotPayload = 0.0f;
			float frostbitePayload = 0.0f;
			float sleepPayload = 0.0f;
			float madnessPayload = 0.0f;
		};

		// Per-hit elemental attack values from weapon/spell magic items (enchantment / spell effects).
		struct ElementalHitComponents
		{
			std::array<float, kDamageTypeCount> attack{};
		};

		// Physical split for the striking weapon (Standard, Strike, Slash, Pierce). Sums to ~1 when resolved.
		using PhysicalWeaponWeights = std::array<float, 4>;

		// --- Hit source (weapon enchant + optional spell from HitData) ---

		// Filled by HitEventHandler + ExtractHitSourceFromWeaponAndSpell when tracing (debug_hit_events).
		struct HitMagicTrace
		{
			enum class WeaponEntry : std::uint8_t
			{
				None = 0,
				AttackingWeapon,
				EquippedSlotMatch
			};
			enum class WeaponEnchant : std::uint8_t
			{
				None = 0,
				InstanceOnEntry,
				BoundWeaponEITM,
				HitDataWeaponFormEITM
			};
			WeaponEntry weaponEntryKind{ WeaponEntry::None };
			WeaponEnchant weaponEnchantKind{ WeaponEnchant::None };
			bool accumulatedSeparateHitSpell{ false };
		};

		// Reads ERCF-tagged effects from the weapon's instance enchantment and/or the hit spell.
		// Buildup: MGEF keywords ERCF.MGEF.Buildup + ERCF.Status.* (magnitude does not deal HP by itself here).
			// Elemental: MGEF keywords ERCF.MGEF.ElementalDamage + ERCF.DamageType.Elem.*
			// (Magic / Fire / Frost / Poison / Lightning; legacy ERCF.DamageType.Elem.Holy → Magic bucket).
		// a_emitExtractDebugLog: when false, skips ERCF[ExtractHitSource] LOG_DEBUG even if debug_hit_events is on
		// (use for cheap probes that pass nullptr weapon entry by design, e.g. projectile branch in OverrideDamageHook).
		void ExtractHitSourceFromWeaponAndSpell(const RE::InventoryEntryData* a_weaponEntry, const RE::MagicItem* a_hitSpell,
			StatusBuildupCoefficients& a_buildupOut, ElementalHitComponents& a_elementalOut,
			const RE::TESObjectWEAP* a_weaponFormFallback = nullptr, HitMagicTrace* a_trace = nullptr);

		// Sums weapon enchant MGEF magnitudes (CK: **percent** of base per line) tagged
		// ERCF.WeaponAttributeScaling.{STR,DEX,INT,FTH,ARC}; stored internally as decimal multipliers (÷100).
		// (see weapon_attribute_scaling.md). Does not read hit spells.
		void AccumulateWeaponAttributeScalingFromMagicItem(const RE::MagicItem* a_item, Math::WeaponAttrScalingCoeffs& a_out);

		// Weapon physical profile: ERCF.DamageType.Phys.* keywords on the weapon if present, else WEAPON_TYPE fallback.
		void ResolvePhysicalWeaponWeights(const RE::TESObjectWEAP* a_weapon, PhysicalWeaponWeights& a_weightsOut);

		// Requirement-v2 bridge (incremental): pick a dominant physical subtype for a hit and return the
		// defender's layer-2 mitigation percents for that subtype.
		//
		// Uses existing `absorptionFractions[subtype]` as mitigation percents (each element is an absorbed fraction in [0,1]).
		[[nodiscard]] DamageTypeId ResolveDominantPhysicalSubtype(const RE::TESObjectWEAP* a_weapon);
		[[nodiscard]] std::vector<float> ExtractLayer2MitigationPercentsForPhysicalSubtype(
			RE::Actor* a_target,
			DamageTypeId a_physicalSubtype,
			const Config::Values& a_cfg);

		// --- Target (worn armor) ---

		// One pass over worn items (no active effects here):
		// - Physical defense: sum armor rating * scale, split per piece using ERCF.DamageType.Phys.* on the ARMO (equal split if none).
		// - Elemental + physical defense/absorption: MGEFs on each worn piece’s **instance enchantment** (magnitude per effect).
		// - Status resist bands: same enchantments.
		// Call `MergeMitigationFromActiveActorEffects` / `MergeStatusResistanceFromActiveActorEffects` after this for skin/race abilities.
		void ExtractFromWornArmor(RE::Actor* a_target, float a_armorRatingDefenseScale, MitigationCoefficients& a_mitigationOut,
			StatusResistanceCoefficients& a_resistOut);

		// After worn armor: add `ERCF.MGEF.Defense` / `ERCF.MGEF.Absorption` + `ERCF.DamageType.*` from **active effects**
		// (race spells, skin abilities, perks). Magnitude matches armor-enchant rules; supports **physical** and elemental.
		// Use for creatures (e.g. dragon: Defense + Slash on a constant ability).
		void MergeMitigationFromActiveActorEffects(RE::Actor* a_target, MitigationCoefficients& a_io);

		// After worn armor enchants: add `ERCF.MGEF.ResBand` from active effects (same magnitude rules).
		void MergeStatusResistanceFromActiveActorEffects(RE::Actor* a_target, StatusResistanceCoefficients& a_io);

		// Per damage type: extra multiplier on damage **taken** after Defense→Absorption.
		// - Base: rating-weighted blend of matchup_taken_{heavy,light,clothing} from ercf.toml using worn ARMO
		//   BOD2 armor type (heavy / light / clothing) and each piece's GetArmorRating().
		// - If no worn armor rating, uses the clothing row (unarmored / flesh baseline).
		// - Then multiplies by each active MGEF tagged ERCF.MGEF.TakenMult + ERCF.DamageType.* (magnitude = multiplier).
		void ExtractTakenDamageMultipliers(RE::Actor* a_target, const Config::Values& a_cfg, std::array<float, kDamageTypeCount>& a_out);

		// requirement.md §2.2: Dawnguard sun keywords on hit spell / weapon enchant → Magic-type component *0 vs non-undead.
		[[nodiscard]] bool HitSourcesHaveSunDamageKeyword(
			const RE::MagicItem* hitSpell,
			const RE::MagicItem* weaponEnchant);

		[[nodiscard]] float Layer2SunMagicScalar(const RE::Actor* victim, bool sourcesHaveSunKeyword);

		// requirement.md §2.2: silver material vs undead — multiplicative bonus on physical D_l2 (tunable constant in .cpp).
		[[nodiscard]] float Layer2SilverVsUndeadPhysicalScalar(const RE::Actor* victim, const RE::TESObjectWEAP* weapon);

		// Convenience: resolve weapon form for a hit (HitData weapon, else attacking inventory entry).
		[[nodiscard]] const RE::TESObjectWEAP* ResolveWeaponFormForHit(
			const RE::HitData* a_hitData,
			const RE::InventoryEntryData* a_attackingWeaponEntry);
	}
}
