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

