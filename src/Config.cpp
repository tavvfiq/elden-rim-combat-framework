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

				s_values.status_resist_cache_ttl_seconds =
					toml::find_or<double>(tbl, "status_resist_cache_ttl_seconds", s_values.status_resist_cache_ttl_seconds);

				s_values.debug_hit_log_enabled =
					toml::find_or<bool>(tbl, "debug_hit_log_enabled", s_values.debug_hit_log_enabled);
				s_values.debug_hit_log_every_n =
					toml::find_or<std::uint32_t>(tbl, "debug_hit_log_every_n", s_values.debug_hit_log_every_n);

				s_values.debug_hit_log_require_console_target_match =
					toml::find_or<bool>(
						tbl,
						"debug_hit_log_require_console_target_match",
						s_values.debug_hit_log_require_console_target_match);

				s_values.debug_hit_log_log_selection_state =
					toml::find_or<bool>(
						tbl,
						"debug_hit_log_log_selection_state",
						s_values.debug_hit_log_log_selection_state);

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

			auto pluginDir = Config::GetPluginDirectory();
			if (!pluginDir || pluginDir->empty()) {
				ERCFLog::Line("Config: plugin directory not available; toml not read");
				return;
			}
			auto path = *pluginDir / "ercf.toml";
			ERCFLog::LineF("Config: reading toml from '%s'", path.string().c_str());
			if (std::filesystem::exists(path)) {
				LoadFile(path);
			} else {
				ERCFLog::Line("ERCF: ercf.toml not found; using defaults");
			}

			ERCFLog::LineF(
				"Config: effective debug_hit_log_enabled=%s debug_hit_log_every_n=%u status_resist_cache_ttl_seconds=%.3f",
				s_values.debug_hit_log_enabled ? "true" : "false",
				s_values.debug_hit_log_every_n,
				s_values.status_resist_cache_ttl_seconds);
		}

		std::optional<std::filesystem::path> GetPluginDirectory()
		{
			REX::W32::HMODULE hMod = nullptr;

			// Try common name variants.
			const wchar_t* dllNames[] = { L"ercf.dll", L"ERCF.dll" };
			for (const wchar_t* dllName : dllNames) {
				hMod = REX::W32::GetModuleHandleW(dllName);
				if (hMod) break;
			}
			if (!hMod) return std::nullopt;

			wchar_t buf[REX::W32::MAX_PATH]{};
			if (REX::W32::GetModuleFileNameW(hMod, buf, static_cast<std::uint32_t>(std::size(buf))) == 0) {
				return std::nullopt;
			}
			std::filesystem::path p(buf);
			p = p.remove_filename();
			return p;
		}

		const Values& Get()
		{
			return s_values;
		}
	}
}

