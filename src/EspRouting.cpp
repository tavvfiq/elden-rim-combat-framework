#include "pch.h"

#include "EspRouting.h"

#include "Log.h"

#include <algorithm>
#include <optional>

namespace ERCF
{
	namespace Esp
	{
		namespace
		{
			constexpr const char* KW_MGEF_DEFENSE = "ERCF.MGEF.Defense";
			constexpr const char* KW_MGEF_ABSORPTION = "ERCF.MGEF.Absorption";
			constexpr const char* KW_MGEF_BUILDUP = "ERCF.MGEF.Buildup";
			constexpr const char* KW_MGEF_RESBAND = "ERCF.MGEF.ResBand";

			constexpr const char* KW_STATUS_POISON = "ERCF.Status.Poison";
			constexpr const char* KW_STATUS_BLEED = "ERCF.Status.Bleed";

			constexpr const char* KW_BAND_IMMUNITY = "ERCF.ResBand.Immunity";
			constexpr const char* KW_BAND_ROBUSTNESS = "ERCF.ResBand.Robustness";

			// Damage type keywords (contract aligned).
			constexpr const char* KW_DAMAGE_STD = "ERCF.DamageType.Phys.Standard";
			constexpr const char* KW_DAMAGE_STRIKE = "ERCF.DamageType.Phys.Strike";
			constexpr const char* KW_DAMAGE_SLASH = "ERCF.DamageType.Phys.Slash";
			constexpr const char* KW_DAMAGE_PIERCE = "ERCF.DamageType.Phys.Pierce";
			constexpr const char* KW_DAMAGE_MAGIC = "ERCF.DamageType.Elem.Magic";
			constexpr const char* KW_DAMAGE_FIRE = "ERCF.DamageType.Elem.Fire";
			constexpr const char* KW_DAMAGE_LIGHTNING = "ERCF.DamageType.Elem.Lightning";
			constexpr const char* KW_DAMAGE_HOLY = "ERCF.DamageType.Elem.Holy";

			[[nodiscard]] bool HasEffectKW(const RE::EffectSetting* a_setting, const char* a_kw)
			{
				return a_setting && a_setting->HasKeywordString(a_kw);
			}

			[[nodiscard]] std::optional<DamageTypeId> ParseDamageType(const RE::EffectSetting* a_setting)
			{
				if (!a_setting) {
					return std::nullopt;
				}

				if (HasEffectKW(a_setting, KW_DAMAGE_STD)) return DamageTypeId::Standard;
				if (HasEffectKW(a_setting, KW_DAMAGE_STRIKE)) return DamageTypeId::Strike;
				if (HasEffectKW(a_setting, KW_DAMAGE_SLASH)) return DamageTypeId::Slash;
				if (HasEffectKW(a_setting, KW_DAMAGE_PIERCE)) return DamageTypeId::Pierce;
				if (HasEffectKW(a_setting, KW_DAMAGE_MAGIC)) return DamageTypeId::Magic;
				if (HasEffectKW(a_setting, KW_DAMAGE_FIRE)) return DamageTypeId::Fire;
				if (HasEffectKW(a_setting, KW_DAMAGE_LIGHTNING)) return DamageTypeId::Lightning;
				if (HasEffectKW(a_setting, KW_DAMAGE_HOLY)) return DamageTypeId::Holy;

				return std::nullopt;
			}

			[[nodiscard]] bool HasActiveTrueCondition(const RE::ActiveEffect* a_effect)
			{
				if (!a_effect) return false;
				// Match the pattern used in other CommonLibSSE-NG mods.
				return a_effect->conditionStatus.get() == RE::ActiveEffect::ConditionStatus::kTrue;
			}

			[[nodiscard]] float AbsorptionMagnitudeToFraction(float a_magnitudePercent)
			{
				// Contract: abs = clamp01(magnitude / 100)
				const float absFrac = a_magnitudePercent / 100.0f;
				return std::clamp(absFrac, 0.0f, 0.999f);
			}

			[[nodiscard]] RE::BSSimpleList<RE::ActiveEffect*>* GetActiveEffects(const RE::Actor* a_actor)
			{
				if (!a_actor) return nullptr;
				auto* actor = const_cast<RE::Actor*>(a_actor);
				auto* magicTarget = actor->AsMagicTarget();
				return magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			}
		}

		void ExtractMitigationFromActiveEffects(const RE::Actor* a_target, MitigationCoefficients& a_out)
		{
			if (!a_target) return;

			auto* list = GetActiveEffects(a_target);
			if (!list) return;

			for (const auto* effect : *list) {
				if (!effect) continue;
				if (!HasActiveTrueCondition(effect)) continue;

				auto* setting = effect->GetBaseObject();
				if (!setting) continue;

				const auto damageType = ParseDamageType(setting);
				if (!damageType.has_value()) continue;

				const float magnitude = effect->GetMagnitude();
				const auto idx = static_cast<std::size_t>(damageType.value());

				if (HasEffectKW(setting, KW_MGEF_DEFENSE)) {
					a_out.defense[idx] += magnitude;
				} else if (HasEffectKW(setting, KW_MGEF_ABSORPTION)) {
					a_out.absorptionFractions[idx].push_back(AbsorptionMagnitudeToFraction(magnitude));
				}
			}
		}

		void ExtractStatusResistancesFromActiveEffects(const RE::Actor* a_target, StatusResistanceCoefficients& a_out)
		{
			if (!a_target) return;

			auto* list = GetActiveEffects(a_target);
			if (!list) return;

			for (const auto* effect : *list) {
				if (!effect) continue;
				if (!HasActiveTrueCondition(effect)) continue;

				auto* setting = effect->GetBaseObject();
				if (!setting) continue;

				if (!HasEffectKW(setting, KW_MGEF_RESBAND)) continue;

				const float magnitude = effect->GetMagnitude();
				if (HasEffectKW(setting, KW_BAND_IMMUNITY)) {
					a_out.immunityResValue += magnitude;
				} else if (HasEffectKW(setting, KW_BAND_ROBUSTNESS)) {
					a_out.robustnessResValue += magnitude;
				}
			}
		}

		void ExtractStatusPayloadFromActiveEffects(const RE::Actor* a_attacker, StatusBuildupCoefficients& a_out)
		{
			if (!a_attacker) return;

			auto* list = GetActiveEffects(a_attacker);
			if (!list) return;

			for (const auto* effect : *list) {
				if (!effect) continue;
				if (!HasActiveTrueCondition(effect)) continue;

				auto* setting = effect->GetBaseObject();
				if (!setting) continue;

				if (!HasEffectKW(setting, KW_MGEF_BUILDUP)) continue;

				const float magnitude = effect->GetMagnitude();

				if (HasEffectKW(setting, KW_STATUS_POISON)) {
					a_out.poisonPayload += magnitude;
				} else if (HasEffectKW(setting, KW_STATUS_BLEED)) {
					a_out.bleedPayload += magnitude;
				}
			}
		}
	}
}

