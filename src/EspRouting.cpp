#include "pch.h"

#include "EspRouting.h"

#include "Config.h"

#include <RE/B/BSContainer.h>
#include <RE/I/InventoryChanges.h>
#include <RE/T/TESObjectARMO.h>

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
			constexpr const char* KW_MGEF_ELEMENTAL_DAMAGE = "ERCF.MGEF.ElementalDamage";
			constexpr const char* KW_MGEF_TAKEN_MULT = "ERCF.MGEF.TakenMult";

			constexpr const char* KW_STATUS_POISON = "ERCF.Status.Poison";
			constexpr const char* KW_STATUS_BLEED = "ERCF.Status.Bleed";
			constexpr const char* KW_STATUS_ROT = "ERCF.Status.Rot";
			constexpr const char* KW_STATUS_FROSTBITE = "ERCF.Status.Frostbite";
			constexpr const char* KW_STATUS_SLEEP = "ERCF.Status.Sleep";
			constexpr const char* KW_STATUS_MADNESS = "ERCF.Status.Madness";

			constexpr const char* KW_BAND_IMMUNITY = "ERCF.ResBand.Immunity";
			constexpr const char* KW_BAND_ROBUSTNESS = "ERCF.ResBand.Robustness";
			constexpr const char* KW_BAND_FOCUS = "ERCF.ResBand.Focus";
			constexpr const char* KW_BAND_MADNESS = "ERCF.ResBand.Madness";

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

			[[nodiscard]] bool IsElementalType(DamageTypeId a_id)
			{
				return a_id == DamageTypeId::Magic || a_id == DamageTypeId::Fire ||
					a_id == DamageTypeId::Lightning || a_id == DamageTypeId::Holy;
			}

			[[nodiscard]] bool IsPhysicalType(DamageTypeId a_id)
			{
				return a_id == DamageTypeId::Standard || a_id == DamageTypeId::Strike ||
					a_id == DamageTypeId::Slash || a_id == DamageTypeId::Pierce;
			}

			[[nodiscard]] float AbsorptionMagnitudeToFraction(float a_magnitudePercent)
			{
				const float absFrac = a_magnitudePercent / 100.0f;
				return std::clamp(absFrac, 0.0f, 0.999f);
			}

			[[nodiscard]] bool HasActiveTrueCondition(const RE::ActiveEffect* a_effect)
			{
				if (!a_effect) {
					return false;
				}
				return a_effect->conditionStatus.get() == RE::ActiveEffect::ConditionStatus::kTrue;
			}

			// AE: GetActiveEffectList is not declared on RE::Actor (see Actor.h); use MagicTarget vtable.
			[[nodiscard]] RE::BSSimpleList<RE::ActiveEffect*>* GetActorActiveEffectList(const RE::Actor* a_actor)
			{
				if (!a_actor) {
					return nullptr;
				}
				return const_cast<RE::Actor*>(a_actor)->AsMagicTarget()->GetActiveEffectList();
			}

			void NormalizePhysFour(PhysicalWeaponWeights& w)
			{
				float s = w[0] + w[1] + w[2] + w[3];
				if (s <= 0.0f) {
					w.fill(0.25f);
					return;
				}
				for (float& x : w) {
					x /= s;
				}
			}

			void AddPhysKeywordWeights(const RE::BGSKeywordForm* a_kwForm, PhysicalWeaponWeights& w, bool& a_any)
			{
				if (!a_kwForm) {
					return;
				}
				if (a_kwForm->HasKeywordString(KW_DAMAGE_STD)) {
					w[0] += 1.0f;
					a_any = true;
				}
				if (a_kwForm->HasKeywordString(KW_DAMAGE_STRIKE)) {
					w[1] += 1.0f;
					a_any = true;
				}
				if (a_kwForm->HasKeywordString(KW_DAMAGE_SLASH)) {
					w[2] += 1.0f;
					a_any = true;
				}
				if (a_kwForm->HasKeywordString(KW_DAMAGE_PIERCE)) {
					w[3] += 1.0f;
					a_any = true;
				}
			}

			void AccumulateMagicItemForHit(
				const RE::MagicItem* a_item,
				StatusBuildupCoefficients& a_buildup,
				ElementalHitComponents& a_elem)
			{
				if (!a_item) {
					return;
				}

				for (auto* eff : a_item->effects) {
					if (!eff) {
						continue;
					}
					auto* setting = eff->baseEffect;
					if (!setting) {
						continue;
					}

					const float mag = eff->GetMagnitude();

					if (HasEffectKW(setting, KW_MGEF_BUILDUP)) {
						if (HasEffectKW(setting, KW_STATUS_POISON)) {
							a_buildup.poisonPayload += mag;
						}
						if (HasEffectKW(setting, KW_STATUS_BLEED)) {
							a_buildup.bleedPayload += mag;
						}
						if (HasEffectKW(setting, KW_STATUS_ROT)) {
							a_buildup.rotPayload += mag;
						}
						if (HasEffectKW(setting, KW_STATUS_FROSTBITE)) {
							a_buildup.frostbitePayload += mag;
						}
						if (HasEffectKW(setting, KW_STATUS_SLEEP)) {
							a_buildup.sleepPayload += mag;
						}
						if (HasEffectKW(setting, KW_STATUS_MADNESS)) {
							a_buildup.madnessPayload += mag;
						}
					}

					if (HasEffectKW(setting, KW_MGEF_ELEMENTAL_DAMAGE)) {
						const auto dt = ParseDamageType(setting);
						if (dt.has_value() && IsElementalType(*dt)) {
							const auto idx = static_cast<std::size_t>(*dt);
							a_elem.attack[idx] += mag;
						}
					}
				}
			}

			void ApplyArmorEnchantmentToMitigation(const RE::MagicItem* a_ench, MitigationCoefficients& a_out)
			{
				if (!a_ench) {
					return;
				}

				for (auto* eff : a_ench->effects) {
					if (!eff) {
						continue;
					}
					auto* setting = eff->baseEffect;
					if (!setting) {
						continue;
					}

					const auto dt = ParseDamageType(setting);
					if (!dt.has_value()) {
						continue;
					}

					const float mag = eff->GetMagnitude();
					const auto idx = static_cast<std::size_t>(*dt);

					if (HasEffectKW(setting, KW_MGEF_DEFENSE)) {
						a_out.defense[idx] += mag;
					} else if (HasEffectKW(setting, KW_MGEF_ABSORPTION)) {
						a_out.absorptionFractions[idx].push_back(AbsorptionMagnitudeToFraction(mag));
					}
				}
			}

			void ApplyArmorEnchantmentToResist(const RE::MagicItem* a_ench, StatusResistanceCoefficients& a_out)
			{
				if (!a_ench) {
					return;
				}

				for (auto* eff : a_ench->effects) {
					if (!eff) {
						continue;
					}
					auto* setting = eff->baseEffect;
					if (!setting) {
						continue;
					}
					if (!HasEffectKW(setting, KW_MGEF_RESBAND)) {
						continue;
					}

					const float mag = eff->GetMagnitude();
					if (HasEffectKW(setting, KW_BAND_IMMUNITY)) {
						a_out.immunityResValue += mag;
					} else if (HasEffectKW(setting, KW_BAND_ROBUSTNESS)) {
						a_out.robustnessResValue += mag;
					} else if (HasEffectKW(setting, KW_BAND_FOCUS)) {
						a_out.focusResValue += mag;
					} else if (HasEffectKW(setting, KW_BAND_MADNESS)) {
						a_out.madnessResValue += mag;
					}
				}
			}

			class WornArmorVisitor final : public RE::InventoryChanges::IItemChangeVisitor
			{
			public:
				explicit WornArmorVisitor(MitigationCoefficients& a_mit, StatusResistanceCoefficients& a_res,
					float a_armorScale) :
					mit(a_mit),
					res(a_res),
					armorScale(a_armorScale)
				{}

				RE::BSContainer::ForEachResult Visit(RE::InventoryEntryData* a_entry) override
				{
					if (!a_entry) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					auto* obj = a_entry->GetObject();
					if (!obj) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					if (auto* armo = obj->As<RE::TESObjectARMO>()) {
						PhysicalWeaponWeights w{};
						bool anyKw = false;
						AddPhysKeywordWeights(static_cast<const RE::BGSKeywordForm*>(armo), w, anyKw);
						if (!anyKw) {
							w.fill(0.25f);
						} else {
							NormalizePhysFour(w);
						}

						const float rating = armo->GetArmorRating();
						const float contrib = rating * armorScale;
						for (std::size_t i = 0; i < 4; ++i) {
							mit.defense[i] += contrib * w[i];
						}

						if (auto* ench = a_entry->GetEnchantment()) {
							ApplyArmorEnchantmentToMitigation(ench, mit);
							ApplyArmorEnchantmentToResist(ench, res);
						}
					}

					return RE::BSContainer::ForEachResult::kContinue;
				}

				MitigationCoefficients& mit;
				StatusResistanceCoefficients& res;
				float armorScale;
			};

			struct ArmorWeightTotals
			{
				float heavyRating = 0.0f;
				float lightRating = 0.0f;
				float clothingRating = 0.0f;
			};

			class WornArmorWeightVisitor final : public RE::InventoryChanges::IItemChangeVisitor
			{
			public:
				explicit WornArmorWeightVisitor(ArmorWeightTotals& a_totals) :
					totals(a_totals)
				{}

				RE::BSContainer::ForEachResult Visit(RE::InventoryEntryData* a_entry) override
				{
					if (!a_entry) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto* obj = a_entry->GetObject();
					if (!obj) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto* armo = obj->As<RE::TESObjectARMO>();
					if (!armo) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					const float r = armo->GetArmorRating();
					using AT = RE::BGSBipedObjectForm::ArmorType;
					switch (armo->GetArmorType()) {
					case AT::kHeavyArmor:
						totals.heavyRating += r;
						break;
					case AT::kLightArmor:
						totals.lightRating += r;
						break;
					case AT::kClothing:
					default:
						totals.clothingRating += r;
						break;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				}

				ArmorWeightTotals& totals;
			};

			void BlendMatchupFromArmorWeights(const ArmorWeightTotals& w, const Config::Values& cfg, std::array<float, kDamageTypeCount>& a_out)
			{
				const float t = w.heavyRating + w.lightRating + w.clothingRating;
				if (t <= 0.0f) {
					for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
						a_out[i] = cfg.matchup_taken_clothing[i];
					}
					return;
				}

				const float nh = w.heavyRating / t;
				const float nl = w.lightRating / t;
				const float nc = w.clothingRating / t;

				for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
					a_out[i] = nh * cfg.matchup_taken_heavy[i] + nl * cfg.matchup_taken_light[i] +
						nc * cfg.matchup_taken_clothing[i];
				}
			}

			void ApplyActiveTakenMultEffects(const RE::Actor* a_target, std::array<float, kDamageTypeCount>& a_io)
			{
				if (!a_target) {
					return;
				}
				auto* list = GetActorActiveEffectList(a_target);
				if (!list) {
					return;
				}

				for (auto* effect : *list) {
					if (!effect || !HasActiveTrueCondition(effect)) {
						continue;
					}
					auto* setting = effect->GetBaseObject();
					if (!setting || !HasEffectKW(setting, KW_MGEF_TAKEN_MULT)) {
						continue;
					}
					const auto dt = ParseDamageType(setting);
					if (!dt.has_value()) {
						continue;
					}
					const float m = effect->GetMagnitude();
					if (m <= 0.0f) {
						continue;
					}
					const auto idx = static_cast<std::size_t>(*dt);
					a_io[idx] *= m;
				}
			}

			void MergeMitigationFromActiveActorEffectsInternal(const RE::Actor* a_target, MitigationCoefficients& a_io)
			{
				if (!a_target) {
					return;
				}
				auto* list = GetActorActiveEffectList(a_target);
				if (!list) {
					return;
				}

				for (auto* effect : *list) {
					if (!effect || !HasActiveTrueCondition(effect)) {
						continue;
					}
					auto* setting = effect->GetBaseObject();
					if (!setting) {
						continue;
					}
					const auto dt = ParseDamageType(setting);
					if (!dt.has_value()) {
						continue;
					}
					const float mag = effect->GetMagnitude();
					const auto idx = static_cast<std::size_t>(*dt);

					if (HasEffectKW(setting, KW_MGEF_DEFENSE)) {
						a_io.defense[idx] += mag;
					} else if (HasEffectKW(setting, KW_MGEF_ABSORPTION)) {
						a_io.absorptionFractions[idx].push_back(AbsorptionMagnitudeToFraction(mag));
					}
				}
			}

			void MergeStatusResistanceFromActiveActorEffectsInternal(const RE::Actor* a_target, StatusResistanceCoefficients& a_io)
			{
				if (!a_target) {
					return;
				}
				auto* list = GetActorActiveEffectList(a_target);
				if (!list) {
					return;
				}

				for (auto* effect : *list) {
					if (!effect || !HasActiveTrueCondition(effect)) {
						continue;
					}
					auto* setting = effect->GetBaseObject();
					if (!setting || !HasEffectKW(setting, KW_MGEF_RESBAND)) {
						continue;
					}
					const float mag = effect->GetMagnitude();
					if (HasEffectKW(setting, KW_BAND_IMMUNITY)) {
						a_io.immunityResValue += mag;
					} else if (HasEffectKW(setting, KW_BAND_ROBUSTNESS)) {
						a_io.robustnessResValue += mag;
					} else if (HasEffectKW(setting, KW_BAND_FOCUS)) {
						a_io.focusResValue += mag;
					} else if (HasEffectKW(setting, KW_BAND_MADNESS)) {
						a_io.madnessResValue += mag;
					}
				}
			}
		}

		void ExtractHitSourceFromWeaponAndSpell(const RE::InventoryEntryData* a_weaponEntry, const RE::MagicItem* a_hitSpell,
			StatusBuildupCoefficients& a_buildupOut, ElementalHitComponents& a_elementalOut,
			const RE::TESObjectWEAP* a_weaponFormFallback, HitMagicTrace* a_trace)
		{
			const RE::MagicItem* weaponMagic = nullptr;

			if (a_weaponEntry) {
				weaponMagic = a_weaponEntry->GetEnchantment();
				if (weaponMagic) {
					if (a_trace) {
						a_trace->weaponEnchantKind = HitMagicTrace::WeaponEnchant::InstanceOnEntry;
					}
					AccumulateMagicItemForHit(weaponMagic, a_buildupOut, a_elementalOut);
				} else {
					if (const auto* bound = a_weaponEntry->GetObject()) {
						if (const auto* weap = bound->As<RE::TESObjectWEAP>()) {
							if (weap->formEnchanting) {
								weaponMagic = weap->formEnchanting;
								if (a_trace) {
									a_trace->weaponEnchantKind = HitMagicTrace::WeaponEnchant::BoundWeaponEITM;
								}
								AccumulateMagicItemForHit(weaponMagic, a_buildupOut, a_elementalOut);
							}
						}
					}
				}
			}

			// NPCs often lack GetAttackingWeapon(); HitData::weapon + base EITM still carries ERCF buildup.
			if (!weaponMagic && a_weaponFormFallback && a_weaponFormFallback->formEnchanting) {
				weaponMagic = a_weaponFormFallback->formEnchanting;
				if (a_trace) {
					a_trace->weaponEnchantKind = HitMagicTrace::WeaponEnchant::HitDataWeaponFormEITM;
				}
				AccumulateMagicItemForHit(weaponMagic, a_buildupOut, a_elementalOut);
			}

			if (a_hitSpell && a_hitSpell != weaponMagic) {
				if (a_trace) {
					a_trace->accumulatedSeparateHitSpell = true;
				}
				AccumulateMagicItemForHit(a_hitSpell, a_buildupOut, a_elementalOut);
			}
		}

		void ResolvePhysicalWeaponWeights(const RE::TESObjectWEAP* a_weapon, PhysicalWeaponWeights& a_weightsOut)
		{
			a_weightsOut.fill(0.0f);
			bool anyKw = false;
			if (a_weapon) {
				AddPhysKeywordWeights(static_cast<const RE::BGSKeywordForm*>(a_weapon), a_weightsOut, anyKw);
			}
			if (anyKw) {
				NormalizePhysFour(a_weightsOut);
				return;
			}

			if (!a_weapon) {
				a_weightsOut = { 0.25f, 0.25f, 0.25f, 0.25f };
				return;
			}

			using WT = RE::WEAPON_TYPE;
			const auto t = a_weapon->GetWeaponType();

			switch (t) {
			case WT::kHandToHandMelee:
				a_weightsOut[1] = 1.0f;  // Strike
				break;
			case WT::kOneHandSword:
			case WT::kOneHandDagger:
				a_weightsOut[0] = 0.5f;
				a_weightsOut[3] = 0.5f;
				break;
			case WT::kOneHandAxe:
			case WT::kTwoHandAxe:
				a_weightsOut[2] = 1.0f;  // Slash
				break;
			case WT::kOneHandMace:
				a_weightsOut[1] = 1.0f;  // Strike
				break;
			case WT::kTwoHandSword:
				a_weightsOut[0] = 0.5f;
				a_weightsOut[3] = 0.5f;
				break;
			case WT::kBow:
			case WT::kCrossbow:
				a_weightsOut[3] = 1.0f;  // Pierce
				break;
			case WT::kStaff:
				a_weightsOut[0] = 0.5f;
				a_weightsOut[2] = 0.5f;
				break;
			default:
				a_weightsOut[0] = 1.0f;
				break;
			}
			NormalizePhysFour(a_weightsOut);
		}

		DamageTypeId ResolveDominantPhysicalSubtype(const RE::TESObjectWEAP* a_weapon)
		{
			PhysicalWeaponWeights w{};
			ResolvePhysicalWeaponWeights(a_weapon, w);

			std::size_t best = 0;
			float bestW = w[0];
			for (std::size_t i = 1; i < 4; ++i) {
				if (w[i] > bestW) {
					bestW = w[i];
					best = i;
				}
			}
			return static_cast<DamageTypeId>(best);  // 0..3 map to Standard..Pierce
		}

		std::vector<float> ExtractLayer2MitigationPercentsForPhysicalSubtype(
			RE::Actor* a_target,
			DamageTypeId a_physicalSubtype,
			const Config::Values& a_cfg)
		{
			std::vector<float> out{};
			if (!a_target) {
				return out;
			}
			const auto idx = static_cast<std::size_t>(a_physicalSubtype);
			if (idx >= 4) {
				return out;
			}

			MitigationCoefficients mit{};
			StatusResistanceCoefficients res{};
			ExtractFromWornArmor(a_target, a_cfg.armor_rating_defense_scale, mit, res);
			MergeMitigationFromActiveActorEffects(a_target, mit);
			return mit.absorptionFractions[idx];
		}

		void ExtractFromWornArmor(RE::Actor* a_target, float a_armorRatingDefenseScale, MitigationCoefficients& a_mitigationOut,
			StatusResistanceCoefficients& a_resistOut)
		{
			if (!a_target) {
				return;
			}

			WornArmorVisitor visitor(a_mitigationOut, a_resistOut, a_armorRatingDefenseScale);
			if (auto* changes = a_target->GetInventoryChanges()) {
				changes->VisitWornItems(visitor);
			}
		}

		void MergeMitigationFromActiveActorEffects(RE::Actor* a_target, MitigationCoefficients& a_io)
		{
			MergeMitigationFromActiveActorEffectsInternal(a_target, a_io);
		}

		void MergeStatusResistanceFromActiveActorEffects(RE::Actor* a_target, StatusResistanceCoefficients& a_io)
		{
			MergeStatusResistanceFromActiveActorEffectsInternal(a_target, a_io);
		}

		void ExtractTakenDamageMultipliers(RE::Actor* a_target, const Config::Values& a_cfg, std::array<float, kDamageTypeCount>& a_out)
		{
			for (float& x : a_out) {
				x = 1.0f;
			}
			if (!a_target) {
				return;
			}

			ArmorWeightTotals w{};
			WornArmorWeightVisitor wvis(w);
			if (auto* changes = a_target->GetInventoryChanges()) {
				changes->VisitWornItems(wvis);
			}
			BlendMatchupFromArmorWeights(w, a_cfg, a_out);
			ApplyActiveTakenMultEffects(a_target, a_out);

			// Taken mult is a non-negative damage multiplier; negative values would invert
			// ModActorValue(kHealth, -dmg) into healing. Clamp config typo / bad MGEF data.
			const float low = std::max(0.0f, a_cfg.matchup_taken_mult_min);
			for (float& x : a_out) {
				x = std::clamp(x, low, a_cfg.matchup_taken_mult_max);
			}
		}

		const RE::TESObjectWEAP* ResolveWeaponFormForHit(
			const RE::HitData* a_hitData,
			const RE::InventoryEntryData* a_attackingWeaponEntry)
		{
			if (a_hitData && a_hitData->weapon) {
				return a_hitData->weapon;
			}
			if (a_attackingWeaponEntry) {
				return a_attackingWeaponEntry->GetObject() ?
					a_attackingWeaponEntry->GetObject()->As<RE::TESObjectWEAP>() :
					nullptr;
			}
			return nullptr;
		}
	}
}
