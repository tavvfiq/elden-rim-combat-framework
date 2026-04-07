#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace ERCF
{
	namespace Config
	{
		// Order: Standard, Strike, Slash, Pierce, Magic, Fire, Frost, Poison, Lightning (matches Esp::DamageTypeId).
		using MatchupTakenRow = std::array<float, 9>;

		struct Values
		{
			// Strict override mode: ERCF suppresses vanilla HP damage and applies its own computed HP damage once.
			// Default off until hooks are proven stable on your runtime.
			bool override_mode_strict = false;
			bool override_debug_log = false;

			// Defense curve constant used by:
			// PostDefense_T = ATK_T^2 / (ATK_T + k_defense * Defense_T)
			float k_defense = 1.0f;

			// Resistance pacing constant used by:
			// EffectivePayload = Payload / (1 + k_resist * ResValue_band)
			float k_resist = 1.0f;

			// Layer-2 damage product is clamped to avoid hard 0 damage forever.
			// DamageFloor_T = max(ATK_T * damage_floor_fraction, damage_floor_min)
			float damage_floor_fraction = 0.001f;
			float damage_floor_min = 0.0f;

			// Status meter decay:
			// - Meter holds for DecayDelay seconds after last buildup
			// - Then universal linear drain: meter -= status_decay_rate * dt (buildup points per real-time second)
			// Set false to freeze meters at their stored values (buildup testing; HUD shows raw storage).
			bool status_meter_decay_enabled = true;
			double status_decay_delay_seconds = 0.75;
			double status_decay_rate = 5.0;
			// Minimum real-time gap between decay applications for a given actor (Actor::Update post-hook). Larger = cheaper.
			float status_decay_tick_interval_seconds = 2.0f;

			// Worn armor: armor rating contribution to physical (Standard/Strike/Slash/Pierce) flat defense.
			// EffectiveDefense_phys += GetArmorRating() * armor_rating_defense_scale * keywordSplit
			float armor_rating_defense_scale = 0.01f;

			// Weapon/spell ERCF.MGEF.ElementalDamage magnitudes are multiplied by this before the Defense→Absorption pipeline.
			float elemental_enchant_damage_scale = 1.0f;

			// Strict override: spell/staff missile path (spell_staff_damage.md). ResolveStaffSpellCatalystBase maps
			// staff/spell tier to a score in [spell_catalyst_base_min, spell_catalyst_base_max] → tier01 for premium lerp.
			float spell_catalyst_base_min = 1.0f;
			float spell_catalyst_base_max = 6.0f;
			// Hands: FinalMag *= 1 + spell_innate_coef * StatSat (INT). Raise this if hand spells feel weak after L1/L2.
			float spell_innate_coef = 3.0f;
			// Elemental missile damage: when HitData has no weapon enchant, ERAS attribute scaling bonus is 0.
			// Add this as a synthetic scaling coeff on the higher of INT vs FTH (same units as weapon § scaling decimals).
			float spell_hand_intrinsic_attr_coef = 1.35f;
			float spell_fallback_scale_coef = 0.9f;
			// Staff: FinalMag *= handMult * lerp(premium_min, premium_max, tier01) * (1 + enchant_k * ScaleCoef * StatSat).
			float spell_staff_tier_premium_min = 1.05f;
			float spell_staff_tier_premium_max = 1.15f;
			float spell_staff_enchant_k = 0.25f;

			// After Defense→Absorption, final damage is multiplied by a per-type "taken" factor.
			// Worn pieces contribute their GetArmorRating() into heavy / light / clothing buckets (BOD2 armor type);
			// the target's multiplier per type T is a rating-weighted blend of the three rows.
			// Naked targets use the clothing row only (flesh/unarmored baseline).
			// Tune in ercf.toml (matchup_heavy_* / matchup_light_* / matchup_clothing_*).
			MatchupTakenRow matchup_taken_heavy{
				1.0f,  // Standard
				1.0f,  // Strike  (use ERCF.MGEF.TakenMult on stone/undead for blunt bonus)
				0.72f, // Slash   — poor vs heavy plate
				0.92f, // Pierce
				1.12f, // Magic   — slightly better vs heavily armored
				1.0f,  // Fire
				1.0f,  // Frost
				1.0f,  // Poison
				1.05f  // Lightning
			};
			MatchupTakenRow matchup_taken_light{
				1.0f,
				0.98f,
				1.1f,
				1.12f,
				0.94f,
				1.0f,
				1.0f,
				1.0f,
				1.0f};
			MatchupTakenRow matchup_taken_clothing{
				1.0f,
				0.92f,
				1.15f,
				1.1f,
				1.0f,
				1.08f,
				1.0f,
				1.0f,
				1.05f};

			// Hard clamp after blending with ERCF.MGEF.TakenMult active effects.
			float matchup_taken_mult_min = 0.05f;
			float matchup_taken_mult_max = 3.0f;

			// Verbose TESHitEvent trace to ERCF.log (sources, mitigation, elemental applications, procs).
			bool debug_hit_events = false;

			// Log each status meter decay step (target, slot, before/after, decay window seconds). Noisy.
			bool debug_status_decay = false;

			// PrismaUI WebView2 overlay for status meters + proc banner (same CreateView-on-kDataLoaded pattern as
			// Souls Style Looting). Default off — WebView2 can freeze on some setups; requires PrismaUI.dll.
			bool enable_prisma_hud = false;
		};

		// Loads config from Data/SKSE/Plugins/<plugin>/ercf.toml (using SKSE log directory).
		// Safe to call on kDataLoaded (idempotent).
		void Load();

		std::optional<std::filesystem::path> GetPluginDirectory();

		// Access resolved config values (defaults if file missing/invalid).
		const Values& Get();
	}
}

