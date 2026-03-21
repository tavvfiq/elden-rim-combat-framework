#include "pch.h"

#include "Config.h"
#include "Log.h"

#include <filesystem>
#include <string>

#include <toml.hpp>

namespace ERCF
{
	namespace Config
	{
		namespace
		{
			Values s_values;
			bool s_loaded = false;
		}

		static void LoadFile(const std::filesystem::path& a_path)
		{
			try {
				auto result = toml::try_parse(a_path.string().c_str());
				if (!result.is_ok()) {
					ERCFLog::Line("ERCF: toml parse failed for ercf.toml, using defaults");
					return;
				}

				const auto& tbl = result.unwrap();
				s_values.k_defense = toml::find_or<float>(tbl, "k_defense", s_values.k_defense);
				s_values.k_resist = toml::find_or<float>(tbl, "k_resist", s_values.k_resist);

				s_values.damage_floor_fraction =
					toml::find_or<float>(tbl, "damage_floor_fraction", s_values.damage_floor_fraction);
				s_values.damage_floor_min =
					toml::find_or<float>(tbl, "damage_floor_min", s_values.damage_floor_min);

				s_values.status_decay_delay_seconds =
					toml::find_or<double>(tbl, "status_decay_delay_seconds", s_values.status_decay_delay_seconds);
				s_values.status_decay_rate =
					toml::find_or<double>(tbl, "status_decay_rate", s_values.status_decay_rate);

				s_values.armor_rating_defense_scale =
					toml::find_or<float>(tbl, "armor_rating_defense_scale", s_values.armor_rating_defense_scale);
				s_values.elemental_enchant_damage_scale =
					toml::find_or<float>(tbl, "elemental_enchant_damage_scale", s_values.elemental_enchant_damage_scale);

				static const char* matchupSuf[] = {
					"standard", "strike", "slash", "pierce", "magic", "fire", "lightning", "holy"};

				for (std::size_t i = 0; i < 8; ++i) {
					const std::string kh = std::string("matchup_heavy_") + matchupSuf[i];
					const std::string kl = std::string("matchup_light_") + matchupSuf[i];
					const std::string kc = std::string("matchup_clothing_") + matchupSuf[i];
					s_values.matchup_taken_heavy[i] = toml::find_or<float>(tbl, kh, s_values.matchup_taken_heavy[i]);
					s_values.matchup_taken_light[i] = toml::find_or<float>(tbl, kl, s_values.matchup_taken_light[i]);
					s_values.matchup_taken_clothing[i] = toml::find_or<float>(tbl, kc, s_values.matchup_taken_clothing[i]);
				}

				s_values.matchup_taken_mult_min =
					toml::find_or<float>(tbl, "matchup_taken_mult_min", s_values.matchup_taken_mult_min);
				s_values.matchup_taken_mult_max =
					toml::find_or<float>(tbl, "matchup_taken_mult_max", s_values.matchup_taken_mult_max);

				s_values.debug_hit_events = toml::find_or<bool>(tbl, "debug_hit_events", s_values.debug_hit_events);

				ERCFLog::Line("ERCF: loaded ercf.toml successfully");
			} catch (...) {
				ERCFLog::Line("ERCF: exception while parsing ercf.toml, using defaults");
			}
		}

		void Load()
		{
			if (s_loaded) {
				return;
			}
			s_loaded = true;

			// Defaults already present in s_values (recommended starting values).
			if (auto logDir = SKSE::log::log_directory()) {
				auto path = *logDir / "ercf.toml";
				if (std::filesystem::exists(path)) {
					LoadFile(path);
				} else {
					ERCFLog::Line("ERCF: ercf.toml not found; using defaults");
				}
			}
		}

		const Values& Get()
		{
			return s_values;
		}
	}
}

