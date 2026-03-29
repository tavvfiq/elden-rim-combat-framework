#include "pch.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>
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
				s_values.override_mode_strict =
					toml::find_or<bool>(tbl, "override_mode_strict", s_values.override_mode_strict);
				s_values.override_debug_log =
					toml::find_or<bool>(tbl, "override_debug_log", s_values.override_debug_log);
				s_values.k_defense = toml::find_or<float>(tbl, "k_defense", s_values.k_defense);
				s_values.k_resist = toml::find_or<float>(tbl, "k_resist", s_values.k_resist);

				s_values.damage_floor_fraction =
					toml::find_or<float>(tbl, "damage_floor_fraction", s_values.damage_floor_fraction);
				s_values.damage_floor_min =
					toml::find_or<float>(tbl, "damage_floor_min", s_values.damage_floor_min);

				s_values.status_meter_decay_enabled =
					toml::find_or<bool>(tbl, "status_meter_decay_enabled", s_values.status_meter_decay_enabled);
				s_values.status_decay_delay_seconds =
					toml::find_or<double>(tbl, "status_decay_delay_seconds", s_values.status_decay_delay_seconds);
				s_values.status_decay_rate =
					toml::find_or<double>(tbl, "status_decay_rate", s_values.status_decay_rate);
				s_values.status_decay_tick_interval_seconds = std::max(
					0.05f,
					toml::find_or<float>(
						tbl,
						"status_decay_tick_interval_seconds",
						s_values.status_decay_tick_interval_seconds));

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
				// Negative "taken mult" makes ApplyTakenDamageMultiplier negative → health ModActorValue heals.
				s_values.matchup_taken_mult_min = std::max(0.0f, s_values.matchup_taken_mult_min);
				if (s_values.matchup_taken_mult_max < s_values.matchup_taken_mult_min) {
					s_values.matchup_taken_mult_max = s_values.matchup_taken_mult_min;
				}

				s_values.debug_hit_events = toml::find_or<bool>(tbl, "debug_hit_events", s_values.debug_hit_events);
				s_values.debug_status_decay =
					toml::find_or<bool>(tbl, "debug_status_decay", s_values.debug_status_decay);
				s_values.enable_prisma_hud =
					toml::find_or<bool>(tbl, "enable_prisma_hud", s_values.enable_prisma_hud);

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
				"Config: effective status_meter_decay_enabled=%s",
				s_values.status_meter_decay_enabled ? "true" : "false");
			ERCFLog::LineF("Config: effective debug_hit_events=%s", s_values.debug_hit_events ? "true" : "false");
			ERCFLog::LineF("Config: effective debug_status_decay=%s", s_values.debug_status_decay ? "true" : "false");
			ERCFLog::LineF("Config: effective override_mode_strict=%s", s_values.override_mode_strict ? "true" : "false");
			ERCFLog::LineF("Config: effective override_debug_log=%s", s_values.override_debug_log ? "true" : "false");
			ERCFLog::LineF("Config: effective enable_prisma_hud=%s", s_values.enable_prisma_hud ? "true" : "false");
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

