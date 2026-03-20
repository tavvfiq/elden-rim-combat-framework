#pragma once

#include <cstdint>

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
		};

		// Loads config from Data/SKSE/Plugins/<plugin>/ercf.toml (using SKSE log directory).
		// Safe to call on kDataLoaded (idempotent).
		void Load();

		// Access resolved config values (defaults if file missing/invalid).
		const Values& Get();
	}
}

