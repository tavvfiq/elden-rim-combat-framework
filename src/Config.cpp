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
					LOG_WARN("ERCF: toml parse failed for ercf.toml, using defaults");
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
				s_values.spell_catalyst_base_min =
					toml::find_or<float>(tbl, "spell_catalyst_base_min", s_values.spell_catalyst_base_min);
				s_values.spell_catalyst_base_max =
					toml::find_or<float>(tbl, "spell_catalyst_base_max", s_values.spell_catalyst_base_max);
				if (s_values.spell_catalyst_base_max < s_values.spell_catalyst_base_min) {
					std::swap(s_values.spell_catalyst_base_min, s_values.spell_catalyst_base_max);
				}
				s_values.spell_innate_coef =
					toml::find_or<float>(tbl, "spell_innate_coef", s_values.spell_innate_coef);
				s_values.spell_hand_intrinsic_attr_coef = toml::find_or<float>(
					tbl,
					"spell_hand_intrinsic_attr_coef",
					s_values.spell_hand_intrinsic_attr_coef);
				s_values.spell_hand_intrinsic_attr_coef =
					(std::max)(0.0f, s_values.spell_hand_intrinsic_attr_coef);
				s_values.spell_fallback_scale_coef =
					toml::find_or<float>(tbl, "spell_fallback_scale_coef", s_values.spell_fallback_scale_coef);
				s_values.spell_staff_tier_premium_min = toml::find_or<float>(
					tbl,
					"spell_staff_tier_premium_min",
					s_values.spell_staff_tier_premium_min);
				s_values.spell_staff_tier_premium_max = toml::find_or<float>(
					tbl,
					"spell_staff_tier_premium_max",
					s_values.spell_staff_tier_premium_max);
				if (s_values.spell_staff_tier_premium_max < s_values.spell_staff_tier_premium_min) {
					std::swap(s_values.spell_staff_tier_premium_min, s_values.spell_staff_tier_premium_max);
				}
				s_values.spell_staff_enchant_k =
					toml::find_or<float>(tbl, "spell_staff_enchant_k", s_values.spell_staff_enchant_k);
				s_values.spell_staff_enchant_k = (std::max)(0.0f, s_values.spell_staff_enchant_k);

				static const char* matchupSuf[] = {
					"standard",
					"strike",
					"slash",
					"pierce",
					"magic",
					"fire",
					"frost",
					"poison",
					"lightning"};

				for (std::size_t i = 0; i < std::size(matchupSuf); ++i) {
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

				{
					const std::string logLevelStr = toml::find_or<std::string>(tbl, "log_level", std::string("info"));
					ERCFLog::SetLogLevel(ERCFLog::ParseLogLevel(logLevelStr));
				}

				LOG_INFO("ERCF: loaded ercf.toml successfully");
			} catch (...) {
				LOG_WARN("ERCF: exception while parsing ercf.toml, using defaults");
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
				LOG_WARN("Config: plugin directory not available; toml not read");
				return;
			}
			auto path = *pluginDir / "ercf.toml";
			LOG_INFO("Config: reading toml from '{}'", path.string());
			if (std::filesystem::exists(path)) {
				LoadFile(path);
			} else {
				LOG_INFO("ERCF: ercf.toml not found; using defaults");
			}

			LOG_INFO("Config: effective status_meter_decay_enabled={}", s_values.status_meter_decay_enabled);
			LOG_INFO("Config: effective debug_hit_events={}", s_values.debug_hit_events);
			LOG_INFO(
				"Config: effective log_level={}",
				ERCFLog::LevelConfigString(ERCFLog::GetLogLevel()));
			LOG_INFO("Config: effective debug_status_decay={}", s_values.debug_status_decay);
			LOG_INFO("Config: effective override_mode_strict={}", s_values.override_mode_strict);
			LOG_INFO("Config: effective override_debug_log={}", s_values.override_debug_log);
			LOG_INFO("Config: effective enable_prisma_hud={}", s_values.enable_prisma_hud);
			LOG_INFO(
				"Config: effective spell_catalyst_base_min={} spell_catalyst_base_max={}",
				s_values.spell_catalyst_base_min,
				s_values.spell_catalyst_base_max);
			LOG_INFO("Config: effective spell_innate_coef={}", s_values.spell_innate_coef);
			LOG_INFO(
				"Config: effective spell_hand_intrinsic_attr_coef={}",
				s_values.spell_hand_intrinsic_attr_coef);
			LOG_INFO("Config: effective spell_fallback_scale_coef={}", s_values.spell_fallback_scale_coef);
			LOG_INFO(
				"Config: effective spell_staff_tier_premium_min={} spell_staff_tier_premium_max={} spell_staff_enchant_k={}",
				s_values.spell_staff_tier_premium_min,
				s_values.spell_staff_tier_premium_max,
				s_values.spell_staff_enchant_k);
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

