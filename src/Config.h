#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace ERCF
{
	namespace Config
	{
		struct Values
		{
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
			// - Then meter decays by meter *= exp(-decay_rate * dt)
			double status_decay_delay_seconds = 0.75;
			double status_decay_rate = 4.0;

			// Performance middle-ground:
			// Cache per-actor summed resistance for a short time so we don't
			// re-walk all ResBand active effects on every single TESHitEvent.
			// If equipment/passives change, worst-case behavior is a short delay
			// (TTL seconds) until the new resistance is observed.
			double status_resist_cache_ttl_seconds = 0.2;

			// Debugging:
			// - When enabled, ERCF writes one log line per non-blocked TESHitEvent.
			// - This can be extremely spammy in combat, so keep it off by default.
			bool debug_hit_log_enabled = false;
			// When debug_hit_log_enabled=true, log only every Nth hit (N>=1).
			std::uint32_t debug_hit_log_every_n = 1;

			// When true, hit logs are written only if RE::Console::GetSelectedRef()
			// matches the current hit-event target actor reference.
			bool debug_hit_log_require_console_target_match = true;

			// When true, hit logs are emitted even if the console-selected ref does NOT
			// match the target actor, and the log includes both formIDs and a match flag.
			// Useful for diagnosing why you see no logs.
			bool debug_hit_log_log_selection_state = false;
		};

		// Loads config from the same directory as `ercf.dll` (Data/SKSE/Plugins/<plugin>/).
		// Safe to call on kDataLoaded (idempotent).
		void Load();

		std::optional<std::filesystem::path> GetPluginDirectory();

		// Access resolved config values (defaults if file missing/invalid).
		const Values& Get();
	}
}

