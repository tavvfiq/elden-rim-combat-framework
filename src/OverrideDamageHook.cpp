#include "pch.h"

#include "OverrideDamageHook.h"

#include "CombatMath.h"
#include "Config.h"
#include "ERASIntegration.h"
#include "EspRouting.h"
#include "Log.h"
#include "Proc.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/A/ActorState.h>
#include <RE/H/hkpAllCdPointCollector.h>
#include <RE/H/HitData.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/M/MagicItem.h>
#include <RE/P/Projectile.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESAmmo.h>
#include <RE/T/TESHavokUtilities.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectWEAP.h>

#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace ERCF::Override
{
	namespace
	{
		// Re-entrancy guard: ERCF applies HP once and must not recurse through the hook.
		thread_local bool s_inERCFApply = false;

		// OCF (Object Categorization Framework) tags actors under KYWD editorIDs like
		// OCF_RaceCreature, OCF_RaceAnimal_*, OCF_RaceUndead_*, etc. on the NPC_ and/or RACE.
		// https://github.com/GroundAura/Object-Categorization-Framework/wiki/Keyword-Reference
		[[nodiscard]] bool KeywordSpanHasEditorIDPrefix(std::span<RE::BGSKeyword* const> a_kws, std::string_view a_prefix)
		{
			for (RE::BGSKeyword* k : a_kws) {
				if (!k) {
					continue;
				}
				const char* edid = k->GetFormEditorID();
				if (!edid || !*edid) {
					continue;
				}
				if (std::string_view{ edid }.starts_with(a_prefix)) {
					return true;
				}
			}
			return false;
		}

		// Same keyword union as TESNPC::HasApplicableKeywordString (NPC_ + RACE).
		[[nodiscard]] bool NpcApplicableKeywordsHavePrefix(RE::TESNPC* a_npc, std::string_view a_prefix)
		{
			if (!a_npc) {
				return false;
			}
			if (KeywordSpanHasEditorIDPrefix(a_npc->GetKeywords(), a_prefix)) {
				return true;
			}
			if (const auto* race = a_npc->GetRace()) {
				return KeywordSpanHasEditorIDPrefix(race->GetKeywords(), a_prefix);
			}
			return false;
		}

		[[nodiscard]] bool VictimEligibleForDamageOverride(RE::Actor* a_victim)
		{
			if (!a_victim) {
				return false;
			}
			if (a_victim->IsPlayerRef()) {
				return true;
			}
			if (a_victim->HasKeywordString("ActorTypeNPC")) {
				return true;
			}
			// Vanilla wildlife/monsters (often lack ActorTypeNPC).
			if (a_victim->HasKeywordString("ActorTypeCreature")) {
				return true;
			}
			if (NpcApplicableKeywordsHavePrefix(a_victim->GetActorBase(), "OCF_Race")) {
				return true;
			}
			return false;
		}

		// Strict melee raw: humanoids use weapon form attack damage (not vanilla rolled HitData); everyone else
		// uses engine unarmed damage (creatures / ActorTypeCreature / OCF non-humanoid races).
		[[nodiscard]] bool AggressorIsHumanoidForMeleeWeaponBase(RE::Actor* a_aggressor)
		{
			if (!a_aggressor) {
				return false;
			}
			if (a_aggressor->IsPlayerRef()) {
				return true;
			}
			if (a_aggressor->HasKeywordString("ActorTypeNPC")) {
				return true;
			}
			return NpcApplicableKeywordsHavePrefix(a_aggressor->GetActorBase(), "OCF_RaceHumanoid");
		}

		[[nodiscard]] float ResolveMeleePhysicalRawForRequirement(
			RE::Actor* a_aggressor,
			const RE::HitData& a_hit,
			float a_vanillaPhysFallback)
		{
			if (!a_aggressor) {
				return (std::max)(0.0f, a_vanillaPhysFallback);
			}
			if (AggressorIsHumanoidForMeleeWeaponBase(a_aggressor) && a_hit.weapon) {
				const float w = a_hit.weapon->GetAttackDamage();
				if (w > 1e-4f) {
					return w;
				}
			}
			const float u = a_aggressor->CalcUnarmedDamage();
			if (u > 1e-4f) {
				return u;
			}
			return (std::max)(0.0f, a_vanillaPhysFallback);
		}

		[[nodiscard]] float ResolveAggressorWeaponWeightForBlock(const RE::HitData& a_hit)
		{
			if (!a_hit.weapon) {
				return 0.0f;
			}
			return a_hit.weapon->GetWeight();
		}

		[[nodiscard]] bool AggressorIsSprinting(RE::Actor* a_aggressor)
		{
			if (!a_aggressor) {
				return false;
			}
			if (auto* st = a_aggressor->AsActorState()) {
				return st->IsSprinting();
			}
			return false;
		}

		// requirement.md §5.3 — shield: left-hand shield armor rating; else §5.1 on defender weapon weight.
		[[nodiscard]] float ResolveBlockGuardPoint(RE::Actor* a_victim, const RE::HitData& a_hit)
		{
			if (!a_victim) {
				return 0.0f;
			}
			const auto guardFromWeap = [](const RE::TESObjectWEAP* w) -> float {
				if (!w) {
					return 0.0f;
				}
				return Math::BlockStaminaBaseFromWeaponWeight(w->GetWeight());
			};
			if (const auto* form = a_victim->GetEquippedObject(true)) {
				if (const auto* ar = form->As<RE::TESObjectARMO>()) {
					if (ar->HasKeywordString("ArmorShield")) {
						// GetArmorRating is non-const in CommonLib; form is const from GetEquippedObject.
						const float arating = static_cast<float>(
							const_cast<RE::TESObjectARMO*>(ar)->GetArmorRating());
						return (std::max)(0.0f, arating);
					}
				}
			}
			if (a_hit.flags.any(RE::HitData::Flag::kBlockWithWeapon)) {
				if (const auto* form = a_victim->GetEquippedObject(true)) {
					if (const auto* w = form->As<RE::TESObjectWEAP>()) {
						return guardFromWeap(w);
					}
				}
			}
			if (const auto* form = a_victim->GetEquippedObject(false)) {
				if (const auto* w = form->As<RE::TESObjectWEAP>()) {
					return guardFromWeap(w);
				}
			}
			return 0.0f;
		}

		// requirement.md §1 Pierce: +30% physical raw into L1 when victim is exposed.
		[[nodiscard]] bool VictimExposesPierceVulnerability(RE::Actor* a_victim)
		{
			if (!a_victim) {
				return false;
			}
			if (a_victim->IsAttacking()) {
				return true;
			}
			auto* st = a_victim->AsActorState();
			if (!st) {
				return false;
			}
			if (st->IsSprinting()) {
				return true;
			}
			if (st->actorState2.staggered) {
				return true;
			}
			return st->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal;
		}

		// critical_and_sneak_damage.md — motion value by weapon keyword (bow 1.0, no crit anim note).
		[[nodiscard]] float ResolveCriticalMotionValue(const RE::TESObjectWEAP* a_weapon)
		{
			if (!a_weapon) {
				return 3.45f;
			}
			if (a_weapon->HasKeywordString("WeapTypeBow") || a_weapon->HasKeywordString("WeapTypeCrossbow")) {
				return 1.0f;
			}
			if (a_weapon->HasKeywordString("WeapTypeDagger")) {
				return 4.20f;
			}
			if (a_weapon->HasKeywordString("WeapTypeGreatsword")) {
				return 2.75f;
			}
			if (a_weapon->HasKeywordString("WeapTypeWarhammer")) {
				return 2.63f;
			}
			if (a_weapon->HasKeywordString("WeapTypeMace") || a_weapon->HasKeywordString("WeapTypeWarAxe")) {
				return 3.03f;
			}
			if (a_weapon->HasKeywordString("WeapTypeSword") || a_weapon->HasKeywordString("WeapTypeKatana")) {
				return 3.45f;
			}
			return 3.45f;
		}

		[[nodiscard]] float ComputeRequirementDamageForVictim(
			RE::Actor* a_victim,
			const RE::HitData& a_hitData,
			float a_rawVanillaDamage,
			float a_spellElementalVanillaScale = 1.0f,
			const RE::MagicItem* a_elementalMagicItemOverride = nullptr)
		{
			if (!a_victim) {
				return 0.0f;
			}

			const auto& cfg = ERCF::Config::Get();

			float physicalL1Core = 0.0f;
			std::array<float, ERCF::Math::kReqElementalL1Count> elementalL1Core{};

			ERAS_API::PlayerStatsSnapshot snap{};
			const bool haveEras = ERCF::ERAS::TryGetActorSnapshot(a_victim, snap);
			if (haveEras) {
				// ERAS DefenseSheet is authoritative per-type L1 defense (physical + five elemental lines).
				physicalL1Core = snap.defense.physical;
				elementalL1Core[0] = snap.defense.magic;
				elementalL1Core[1] = snap.defense.fire;
				elementalL1Core[2] = snap.defense.frost;
				elementalL1Core[3] = snap.defense.poison;
				elementalL1Core[4] = snap.defense.lightning;
			} else {
				LOG_INFO(
					"ERCF: ERAS not available for strict-override L1; "
					"using Req_ComputeDefenseBucketsL1 (kHealth / kMagicka / kDamageResist)");
				float level = static_cast<float>(a_victim->GetLevel());
				float maxHP = 0.0f;
				float maxMP = 0.0f;
				float armorRating = 0.0f;
				if (auto* av = a_victim->AsActorValueOwner()) {
					maxHP = av->GetActorValue(RE::ActorValue::kHealth);
					maxMP = av->GetActorValue(RE::ActorValue::kMagicka);
					armorRating = av->GetActorValue(RE::ActorValue::kDamageResist);
				}
				const auto buckets = ERCF::Math::Req_ComputeDefenseBucketsL1(level, maxHP, maxMP, armorRating);
				physicalL1Core = buckets.physical;
				elementalL1Core = buckets.elemental;
			}

			// Gather current L2 mitigation sources (existing absorption fractions) once.
			ERCF::Esp::MitigationCoefficients mit{};
			ERCF::Esp::StatusResistanceCoefficients resistUnused{};
			ERCF::Esp::ExtractFromWornArmor(a_victim, cfg.armor_rating_defense_scale, mit, resistUnused);
			ERCF::Esp::MergeMitigationFromActiveActorEffects(a_victim, mit);

			const RE::MagicItem* weaponEnchant = nullptr;
			if (a_hitData.weapon && a_hitData.weapon->formEnchanting) {
				weaponEnchant = a_hitData.weapon->formEnchanting;
			}
			const RE::MagicItem* hitMagicItem = a_elementalMagicItemOverride ?
				a_elementalMagicItemOverride :
				static_cast<RE::MagicItem*>(a_hitData.attackDataSpell);
			const bool sunSources = ERCF::Esp::HitSourcesHaveSunDamageKeyword(hitMagicItem, weaponEnchant);
			const float sunMagicScalar = ERCF::Esp::Layer2SunMagicScalar(a_victim, sunSources);
			const float silverPhysScalar =
				ERCF::Esp::Layer2SilverVsUndeadPhysicalScalar(a_victim, a_hitData.weapon);

			ERCF::Esp::StatusBuildupCoefficients buildupExtract{};
			ERCF::Esp::ElementalHitComponents elem{};
			const RE::TESObjectWEAP* weaponFallback = a_hitData.weapon;
			ERCF::Esp::ExtractHitSourceFromWeaponAndSpell(
				nullptr,
				hitMagicItem,
				buildupExtract,
				elem,
				weaponFallback,
				nullptr);

			bool anyElemental = false;
			for (std::size_t i = static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic);
				 i < ERCF::Esp::kDamageTypeCount;
				 ++i) {
				if (elem.attack[i] > 0.0f) {
					anyElemental = true;
					break;
				}
			}
			// Prefer HitData::physicalDamage when the engine filled it (melee); else caller's raw (e.g. captured
			// totalDamage fallback before totalDamage was cleared, or projectile estimate).
			float physRawResolved = a_rawVanillaDamage;
			if (a_hitData.physicalDamage > 1e-4f) {
				physRawResolved = a_hitData.physicalDamage;
			}

			if (physRawResolved <= 0.0f && !anyElemental) {
				return 0.0f;
			}

			float total = 0.0f;

			RE::Actor* aggressor = nullptr;
			if (auto nipAgg = a_hitData.aggressor.get()) {
				aggressor = nipAgg.get();
			}

			Math::WeaponAttrScalingCoeffs attrCoeff{};
			if (weaponEnchant) {
				ERCF::Esp::AccumulateWeaponAttributeScalingFromMagicItem(weaponEnchant, attrCoeff);
			}
			// Hand / no-weapon spell missiles: no formEnchanting → no STR/DEX/INT lines; L1 then eats tiny raw.
			// Synthetic INT or FTH coeff (weapon_attribute_scaling.md units) matches staff-like stat contribution.
			Math::WeaponAttrScalingCoeffs elemAttrCoeff = attrCoeff;
			ERAS_API::PlayerStatsSnapshot atkSnap{};
			const bool haveAtkSnap =
				aggressor != nullptr && ERCF::ERAS::TryGetActorSnapshot(aggressor, atkSnap);
			if (!weaponEnchant && haveAtkSnap && cfg.spell_hand_intrinsic_attr_coef > 1e-6f) {
				if (atkSnap.attrs.fth > atkSnap.attrs.intl) {
					elemAttrCoeff.fth += cfg.spell_hand_intrinsic_attr_coef;
				} else {
					elemAttrCoeff.intl += cfg.spell_hand_intrinsic_attr_coef;
				}
			}

			// Physical: L1 uses ERAS DefenseSheet physical or Req bucket only (no MGEF flat on L1).
			const auto subtype = ERCF::Esp::ResolveDominantPhysicalSubtype(a_hitData.weapon);

			struct ReqDamageTrace
			{
				// physical
				float physResolved = 0.0f;
				float sneakMult = 1.0f;
				float rawPhysIn = 0.0f;
				float attrBonusPhys = 0.0f;
				float rawPhysAfterAttr = 0.0f;
				float critMult = 1.0f;
				float motionValue = 1.0f;
				float rawPhysAfterCrit = 0.0f;
				float rawPhysAfterVuln = 0.0f;
				float physPostL1 = 0.0f;
				float physPostL2 = 0.0f;
				float physPostL2Scaled = 0.0f;
				std::uint32_t physL2Sources = 0u;

				// elemental (index by Esp::DamageTypeId; only 4..8 are used)
				std::array<float, ERCF::Esp::kDamageTypeCount> elemBaseScaled{};
				std::array<float, ERCF::Esp::kDamageTypeCount> attrBonusElem{};
				std::array<float, ERCF::Esp::kDamageTypeCount> rawElemAfterAttr{};
				std::array<float, ERCF::Esp::kDamageTypeCount> elemPostL1{};
				std::array<float, ERCF::Esp::kDamageTypeCount> elemPostL2{};
				std::array<float, ERCF::Esp::kDamageTypeCount> elemPostL2Scaled{};
				std::array<std::uint32_t, ERCF::Esp::kDamageTypeCount> elemL2Sources{};
			};

			ReqDamageTrace trace{};
			ReqDamageTrace* tr = cfg.override_debug_log ? &trace : nullptr;

			if (physRawResolved > 0.0f) {
				const float sneakMult =
					a_hitData.flags.any(RE::HitData::Flag::kSneakAttack) ? 1.2f : 1.0f;
				const float baseForAttr = physRawResolved * sneakMult;
				if (tr) {
					tr->physResolved = physRawResolved;
					tr->sneakMult = sneakMult;
					tr->rawPhysIn = baseForAttr;
				}
					float attrBonusPhys = 0.0f;
				if (haveAtkSnap) {
					attrBonusPhys = Math::WeaponAttrScalingBonusDamage(
						baseForAttr,
						attrCoeff,
						atkSnap.attrs.str,
						atkSnap.attrs.dex,
						atkSnap.attrs.intl,
						atkSnap.attrs.fth,
						atkSnap.attrs.arc);
				}
				if (tr) {
					tr->attrBonusPhys = attrBonusPhys;
				}
				float rawPhys = baseForAttr + attrBonusPhys;
				if (tr) {
					tr->rawPhysAfterAttr = rawPhys;
				}
				const bool isCrit = a_hitData.flags.any(RE::HitData::Flag::kCritical) &&
					!a_hitData.flags.any(RE::HitData::Flag::kIgnoreCritical);
				if (isCrit) {
					const float mv = ResolveCriticalMotionValue(a_hitData.weapon);
					// (std::max)(0, x) only rejects negative garbage. Skyrim often leaves criticalDamageMult at 0
					// at this pre-commit call site even when kCritical is set; multiplying by 0 would wipe phys.
					float cm = (std::max)(0.0f, a_hitData.criticalDamageMult);
					if (cm <= 1e-4f) {
						cm = 1.0f;
					}
					if (tr) {
						tr->critMult = cm;
						tr->motionValue = mv;
					}
					rawPhys *= cm * mv;
				} else if (tr) {
					tr->critMult = 1.0f;
					tr->motionValue = 1.0f;
				}
				if (tr) {
					tr->rawPhysAfterCrit = rawPhys;
				}
				if (subtype == ERCF::Esp::DamageTypeId::Pierce && VictimExposesPierceVulnerability(a_victim)) {
					rawPhys *= 1.3f;
				}
				if (tr) {
					tr->rawPhysAfterVuln = rawPhys;
				}
				const float postL1 = ERCF::Math::Req_ApplyLayer1Clamp(rawPhys, physicalL1Core);
				if (tr) {
					tr->physPostL1 = postL1;
				}
				const auto idx = static_cast<std::size_t>(subtype);
				const auto& percents = (idx < ERCF::Esp::kDamageTypeCount) ? mit.absorptionFractions[idx] : std::vector<float>{};
				if (tr) {
					tr->physL2Sources = static_cast<std::uint32_t>(percents.size());
				}
				float phys = ERCF::Math::Req_ApplyLayer2Mitigation(postL1, percents);
				if (tr) {
					tr->physPostL2 = phys;
				}
				phys *= silverPhysScalar;
				if (tr) {
					tr->physPostL2Scaled = phys;
				}
				total += phys;
			}

			// Elemental: L1 per type from ERAS sheet or Req only; same weapon attribute scaling as physical
			// (bonus = floor(base × coeff × saturation) per stat, summed) on each type’s scaled MGEF base.
			// a_spellElementalVanillaScale applies spell_staff_damage.md catalyst / bare-hands factor to MGEF bases.
			{
				for (std::size_t i = static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic);
					 i < ERCF::Esp::kDamageTypeCount;
					 ++i) {
					const float rawElemBase = elem.attack[i] * cfg.elemental_enchant_damage_scale *
						a_spellElementalVanillaScale;
					if (tr) {
						tr->elemBaseScaled[i] = rawElemBase;
					}
					if (!(rawElemBase > 0.0f)) {
						continue;
					}
					float attrBonusElem = 0.0f;
					if (haveAtkSnap) {
						attrBonusElem = Math::WeaponAttrScalingBonusDamage(
							rawElemBase,
							elemAttrCoeff,
							atkSnap.attrs.str,
							atkSnap.attrs.dex,
							atkSnap.attrs.intl,
							atkSnap.attrs.fth,
							atkSnap.attrs.arc);
					}
					if (tr) {
						tr->attrBonusElem[i] = attrBonusElem;
					}
					const float rawElem = rawElemBase + attrBonusElem;
					if (tr) {
						tr->rawElemAfterAttr[i] = rawElem;
					}
					const std::size_t eSlot = i - static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic);
					const float elemCore = (eSlot < ERCF::Math::kReqElementalL1Count) ?
						elementalL1Core[eSlot] :
						0.0f;
					const float postL1 = ERCF::Math::Req_ApplyLayer1Clamp(rawElem, elemCore);
					if (tr) {
						tr->elemPostL1[i] = postL1;
						tr->elemL2Sources[i] = static_cast<std::uint32_t>(mit.absorptionFractions[i].size());
					}
					float elemDmg = ERCF::Math::Req_ApplyLayer2Mitigation(postL1, mit.absorptionFractions[i]);
					if (tr) {
						tr->elemPostL2[i] = elemDmg;
					}
					if (i == static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)) {
						elemDmg *= sunMagicScalar;
					}
					if (tr) {
						tr->elemPostL2Scaled[i] = elemDmg;
					}
					total += elemDmg;
				}
			}

			const float frostMult = ERCF::Proc::GetDamageTakenMultiplier(a_victim->GetFormID());
			const float out = (std::max)(0.0f, total * frostMult);

			if (cfg.override_debug_log) {
				LOG_INFO(
					"ERCF[ReqDamage] vic={:08X} agg={:08X} "
					"physResolved={:.3f} sneakMult={:.2f} rawAfterSneak={:.3f} attrPhys={:.3f} rawAfterAttr={:.3f} "
					"critMult={:.3f} motionVal={:.3f} rawAfterCrit={:.3f} rawPhysVuln={:.3f} "
					"physL1Core={:.3f} physPostL1={:.3f} physL2sources={} physPostL2={:.3f} silverPhysScalar={:.3f} physPostL2Scaled={:.3f} "
					"spellElemScale={:.3f} frostMult={:.3f} totalOut={:.3f}",
					a_victim->GetFormID(),
					aggressor ? aggressor->GetFormID() : 0u,
					trace.physResolved,
					trace.sneakMult,
					trace.rawPhysIn,
					trace.attrBonusPhys,
					trace.rawPhysAfterAttr,
					trace.critMult,
					trace.motionValue,
					trace.rawPhysAfterCrit,
					trace.rawPhysAfterVuln,
					physicalL1Core,
					trace.physPostL1,
					trace.physL2Sources,
					trace.physPostL2,
					silverPhysScalar,
					trace.physPostL2Scaled,
					a_spellElementalVanillaScale,
					frostMult,
					out);
				LOG_INFO(
					"ERCF[ReqDamage] elem rawScaled(Ma/Fi/Fr/Po/Li)={:.3f},{:.3f},{:.3f},{:.3f},{:.3f} "
					"attr={:.3f},{:.3f},{:.3f},{:.3f},{:.3f} "
					"raw={:.3f},{:.3f},{:.3f},{:.3f},{:.3f} "
					"l1Core={:.3f},{:.3f},{:.3f},{:.3f},{:.3f} "
					"postL1={:.3f},{:.3f},{:.3f},{:.3f},{:.3f} "
					"l2sources={},{},{},{},{} "
					"postL2={:.3f},{:.3f},{:.3f},{:.3f},{:.3f} "
					"sunScalar={:.3f} postL2Scaled={:.3f},{:.3f},{:.3f},{:.3f},{:.3f}",
					trace.elemBaseScaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.elemBaseScaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.elemBaseScaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.elemBaseScaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.elemBaseScaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)],
					trace.attrBonusElem[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.attrBonusElem[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.attrBonusElem[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.attrBonusElem[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.attrBonusElem[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)],
					trace.rawElemAfterAttr[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.rawElemAfterAttr[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.rawElemAfterAttr[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.rawElemAfterAttr[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.rawElemAfterAttr[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)],
					elementalL1Core[0],
					elementalL1Core[1],
					elementalL1Core[2],
					elementalL1Core[3],
					elementalL1Core[4],
					trace.elemPostL1[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.elemPostL1[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.elemPostL1[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.elemPostL1[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.elemPostL1[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)],
					trace.elemL2Sources[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.elemL2Sources[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.elemL2Sources[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.elemL2Sources[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.elemL2Sources[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)],
					trace.elemPostL2[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.elemPostL2[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.elemPostL2[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.elemPostL2[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.elemPostL2[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)],
					sunMagicScalar,
					trace.elemPostL2Scaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic)],
					trace.elemPostL2Scaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Fire)],
					trace.elemPostL2Scaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Frost)],
					trace.elemPostL2Scaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Poison)],
					trace.elemPostL2Scaled[static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Lightning)]);
			}

			return out;
		}

		// Parry-for-all pattern: VTABLE_ArrowProjectile / VTABLE_MissileProjectile vfunc slot 190
		// (projectile vs Havok collision collector).
		constexpr std::size_t kProjectileCollisionVfuncIndex = 190;

		using ProjectileCollisionFn = void (*)(RE::Projectile*, RE::hkpAllCdPointCollector*);

		[[nodiscard]] RE::Actor* FindActorVictimFromCdCollector(RE::hkpAllCdPointCollector* a_col)
		{
			if (!a_col) {
				return nullptr;
			}
			for (const auto& hit : a_col->hits) {
				auto* refrA = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableA);
				auto* refrB = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableB);
				if (refrA && refrA->GetFormType() == RE::FormType::ActorCharacter) {
					return refrA->As<RE::Actor>();
				}
				if (refrB && refrB->GetFormType() == RE::FormType::ActorCharacter) {
					return refrB->As<RE::Actor>();
				}
			}
			return nullptr;
		}

		// Bow/crossbow (and similar) physical payload from forms only — not engine-perk-rolled damage.
		[[nodiscard]] float ProjectileWeaponAmmoBasePhysical(const RE::Projectile::PROJECTILE_RUNTIME_DATA& rd)
		{
			if (rd.spell || !rd.weaponSource) {
				return 0.0f;
			}
			float sum = rd.weaponSource->GetAttackDamage();
			if (rd.ammoSource) {
				sum += rd.ammoSource->GetRuntimeData().data.damage;
			}
			return sum;
		}

		// Upper bound / branch gate: prefer runtime rolled damage, else weapon+ammo base (Parry-for-all style).
		[[nodiscard]] float EstimateProjectileVanillaPhysicalDamage(RE::Projectile* a_proj)
		{
			if (!a_proj) {
				return 0.0f;
			}
			auto& rd = a_proj->GetProjectileRuntimeData();
			if (rd.weaponDamage > 1e-4f) {
				return rd.weaponDamage;
			}
			return ProjectileWeaponAmmoBasePhysical(rd);
		}

		// Strict-override requirement pipeline applies sneak / attr / crit in ComputeRequirementDamageForVictim.
		// rd.weaponDamage often already includes vanilla sneak, crit, archery perks, etc.; feeding it as "raw"
		// would double-stack with ERCF. Prefer weapon+ammo base when the shot is a physical weapon missile.
		[[nodiscard]] float ResolveProjectilePhysicalRawForRequirement(RE::Projectile* a_proj, float a_engineEstimate)
		{
			if (!a_proj) {
				return a_engineEstimate;
			}
			auto& rd = a_proj->GetProjectileRuntimeData();
			const float base = ProjectileWeaponAmmoBasePhysical(rd);
			if (base > 1e-4f) {
				return base;
			}
			return a_engineEstimate;
		}

		// After vanilla projectile collision, the victim's last HitData usually holds sneak/crit flags and
		// criticalDamageMult (computed before HP is applied). We copy those into our synthetic HitData so
		// ComputeRequirementDamageForVictim matches melee. If the engine skipped flags, sneak uses a small
		// detection fallback; crit has no safe stat-only fallback (perks would false-trigger).
		[[nodiscard]] bool TryCopyRangedSneakCritFromVictimLastHit(
			RE::Actor* a_victim,
			RE::Actor* a_aggressor,
			const RE::TESObjectWEAP* a_weapon,
			RE::HitData& a_hit)
		{
			auto* mh = a_victim ? a_victim->GetMiddleHighProcess() : nullptr;
			if (!mh || !mh->lastHitData) {
				return false;
			}
			const RE::HitData& src = *mh->lastHitData;
			if (auto ah = src.aggressor.get()) {
				if (ah.get() != a_aggressor) {
					return false;
				}
			} else {
				return false;
			}
			if (auto th = src.target.get()) {
				if (th.get() != a_victim) {
					return false;
				}
			}
			if (a_weapon && src.weapon && src.weapon != a_weapon) {
				return false;
			}
			if (src.flags.any(RE::HitData::Flag::kSneakAttack)) {
				a_hit.flags.set(RE::HitData::Flag::kSneakAttack);
			}
			if (src.flags.any(RE::HitData::Flag::kCritical)) {
				a_hit.flags.set(RE::HitData::Flag::kCritical);
			}
			if (src.flags.any(RE::HitData::Flag::kIgnoreCritical)) {
				a_hit.flags.set(RE::HitData::Flag::kIgnoreCritical);
			}
			a_hit.criticalDamageMult = src.criticalDamageMult;
			if (!a_hit.flags.any(RE::HitData::Flag::kIgnoreCritical) &&
				!a_hit.flags.any(RE::HitData::Flag::kCritical) &&
				src.criticalDamageMult > 1.02f) {
				a_hit.flags.set(RE::HitData::Flag::kCritical);
			}
			return true;
		}

		void ApplyRangedSneakFallbackIfNeeded(RE::Actor* a_victim, RE::Actor* a_aggressor, RE::HitData& a_hit)
		{
			if (a_hit.flags.any(RE::HitData::Flag::kSneakAttack)) {
				return;
			}
			if (!a_aggressor || !a_victim) {
				return;
			}
			if (!a_aggressor->IsSneaking()) {
				return;
			}
			// Victim's awareness of the shooter; lower => better for sneak attack eligibility.
			const std::int32_t det = a_victim->RequestDetectionLevel(a_aggressor);
			if (det <= 1) {
				a_hit.flags.set(RE::HitData::Flag::kSneakAttack);
			}
		}

		void ApplyRangedSneakCritToProjectileHit(
			RE::Actor* a_victim,
			RE::Actor* a_aggressor,
			const RE::TESObjectWEAP* a_weapon,
			RE::HitData& a_hit)
		{
			static_cast<void>(TryCopyRangedSneakCritFromVictimLastHit(a_victim, a_aggressor, a_weapon, a_hit));
			ApplyRangedSneakFallbackIfNeeded(a_victim, a_aggressor, a_hit);
		}

		// Same elemental rules as HitEventHandler / ComputeRequirementDamageForVictim, but no inventory entry
		// (projectile runtime only has spell + weaponSource). Suppress ExtractHitSource debug log here: this runs
		// on collision as a predicate and would spam; meaningful traces come from TESHitEvent + strict override path.
		[[nodiscard]] bool ProjectileHasErcfElementalDamage(
			RE::MagicItem* a_spell,
			const RE::TESObjectWEAP* a_weaponFallback)
		{
			if (!a_spell && !a_weaponFallback) {
				return false;
			}
			ERCF::Esp::StatusBuildupCoefficients bu{};
			ERCF::Esp::ElementalHitComponents elem{};
			ERCF::Esp::ExtractHitSourceFromWeaponAndSpell(
				nullptr,
				a_spell,
				bu,
				elem,
				a_weaponFallback,
				nullptr);
			for (std::size_t i = static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic);
				 i < ERCF::Esp::kDamageTypeCount;
				 ++i) {
				if (elem.attack[i] > 0.0f) {
					return true;
				}
			}
			return false;
		}

		// Per-vanilla CK: each MGEF "Minimum Skill Level" is 0 / 25 / 50 / 75 / 100 for the five spell tiers.
		[[nodiscard]] std::int32_t MaxEffectMinimumSkillLevel(const RE::MagicItem* a_item)
		{
			if (!a_item) {
				return 0;
			}
			std::int32_t m = 0;
			for (auto* eff : a_item->effects) {
				if (eff && eff->baseEffect) {
					m = (std::max)(m, eff->baseEffect->GetMinimumSkillLevel());
				}
			}
			return m;
		}

		// Tier score ~1..6 (then clamped to cfg min/max): (1) WeapMaterial* on staff if present, (2) else spell tier
		// skill on a_castSpell (staff missile path passes rd.spell), (3) else staff GetGoldValue() log-lerp,
		// (4) else cfg midpoint. Output always clamped to [a_cfgMin, a_cfgMax].
		[[nodiscard]] float ResolveStaffSpellCatalystBase(
			const RE::TESObjectWEAP* a_staff,
			float a_cfgMin,
			float a_cfgMax,
			const RE::MagicItem* a_castSpell = nullptr)
		{
			if (!a_staff) {
				return 0.5f * (a_cfgMin + a_cfgMax);
			}
			static constexpr struct
			{
				const char* kw;
				float base612;
			} kRows[] = {
				{ "WeapMaterialIron", 1.0f },
				{ "WeapMaterialWood", 1.0f },
				{ "WeapMaterialRustedIron", 1.0f },
				{ "WeapMaterialImperial", 1.5f },
				{ "WeapMaterialSteel", 1.0f },
				{ "WeapMaterialSilver", 1.0f },
				{ "WeapMaterialDraugr", 1.5f },
				{ "WeapMaterialAncientNord", 1.5f },
				{ "WeapMaterialFalmer", 2.0f },
				{ "WeapMaterialDwarven", 2.5f },
				{ "WeapMaterialElven", 2.5f },
				{ "WeapMaterialFalmerHoned", 2.5f },
				{ "WeapMaterialOrcish", 3.0f },
				{ "WeapMaterialNordicCarved", 3.5f },
				{ "WeapMaterialMalachite", 4.0f },
				{ "WeapMaterialGlass", 4.0f },
				{ "WeapMaterialMoonstone", 4.0f },
				{ "WeapMaterialEbony", 4.5f },
				{ "WeapMaterialStalhrim", 5.0f },
				{ "WeapMaterialDaedric", 6.0f },
				{ "WeapMaterialDragonbone", 6.0f },
			};
			float best = -1.0f;
			for (const auto& row : kRows) {
				if (a_staff->HasKeywordString(row.kw)) {
					best = (std::max)(best, row.base612);
				}
			}
			if (best >= 0.0f) {
				return (std::clamp)(best, a_cfgMin, a_cfgMax);
			}

			if (a_castSpell) {
				const std::int32_t skillMax = MaxEffectMinimumSkillLevel(a_castSpell);
				const float s = (std::clamp)(static_cast<float>(skillMax), 0.0f, 100.0f);
				const float fromSpellTier = a_cfgMin + (s / 100.0f) * (a_cfgMax - a_cfgMin);
				return (std::clamp)(fromSpellTier, a_cfgMin, a_cfgMax);
			}

			// Gold value band: tune to vanilla staff prices (cheap novice ~tens–low hundreds, top staves multi-k).
			const float gold = static_cast<float>((std::max)(0, a_staff->GetGoldValue()));
			constexpr float kGoldRefLo = 50.0f;
			constexpr float kGoldRefHi = 4500.0f;
			const float logLo = std::log1p(kGoldRefLo);
			const float logHi = std::log1p(kGoldRefHi);
			if (gold > 0.0f && logHi > logLo) {
				float t = (std::log1p(gold) - logLo) / (logHi - logLo);
				t = (std::clamp)(t, 0.0f, 1.0f);
				const float fromValue = a_cfgMin + t * (a_cfgMax - a_cfgMin);
				return (std::clamp)(fromValue, a_cfgMin, a_cfgMax);
			}
			return 0.5f * (a_cfgMin + a_cfgMax);
		}

		// spell_staff_damage.md: multiplier applied to each ERCF elemental MGEF base for spell missiles.
		[[nodiscard]] float ComputeSpellStaffElementalVanillaScale(
			RE::Actor* a_aggressor,
			const RE::TESObjectWEAP* a_weaponSource,
			const Config::Values& a_cfg,
			const RE::MagicItem* a_castSpellForCatalystTier = nullptr)
		{
			if (!a_aggressor) {
				return 1.0f;
			}
			ERAS_API::PlayerStatsSnapshot atkSnap{};
			if (!ERCF::ERAS::TryGetActorSnapshot(a_aggressor, atkSnap)) {
				return 1.0f;
			}
			const bool catalyst = a_weaponSource != nullptr && a_weaponSource->IsStaff();
			if (catalyst) {
				Math::WeaponAttrScalingCoeffs c{};
				if (a_weaponSource->formEnchanting) {
					ERCF::Esp::AccumulateWeaponAttributeScalingFromMagicItem(a_weaponSource->formEnchanting, c);
				}
				float scaleCoef = a_cfg.spell_fallback_scale_coef;
				std::int32_t primaryStat = atkSnap.attrs.intl;
				if (c.intl > 1e-4f) {
					scaleCoef = c.intl;
					primaryStat = atkSnap.attrs.intl;
				} else if (c.fth > 1e-4f) {
					scaleCoef = c.fth;
					primaryStat = atkSnap.attrs.fth;
				}
				const float statSat = Math::WeaponAttrScalingSaturation(primaryStat);
				const float catalystBase = ResolveStaffSpellCatalystBase(
					a_weaponSource,
					a_cfg.spell_catalyst_base_min,
					a_cfg.spell_catalyst_base_max,
					a_castSpellForCatalystTier);
				const float span = a_cfg.spell_catalyst_base_max - a_cfg.spell_catalyst_base_min;
				float tier01 = 0.5f;
				if (span > 1e-5f) {
					tier01 = (catalystBase - a_cfg.spell_catalyst_base_min) / span;
				}
				tier01 = (std::clamp)(tier01, 0.0f, 1.0f);
				const float handLike = Math::SpellBareHandsScalingMultiplier(a_cfg.spell_innate_coef, statSat);
				return Math::SpellStaffMissileMultiplier(
					handLike,
					tier01,
					a_cfg.spell_staff_tier_premium_min,
					a_cfg.spell_staff_tier_premium_max,
					scaleCoef,
					statSat,
					a_cfg.spell_staff_enchant_k);
			}
			const float statSat = Math::WeaponAttrScalingSaturation(atkSnap.attrs.intl);
			return Math::SpellBareHandsScalingMultiplier(a_cfg.spell_innate_coef, statSat);
		}

		void ProjectileCollisionCommon(
			RE::Projectile* a_proj,
			RE::hkpAllCdPointCollector* a_col,
			REL::Relocation<ProjectileCollisionFn>& a_orig)
		{
			if (!a_proj || !a_col) {
				a_orig(a_proj, a_col);
				return;
			}

			const auto& cfg = ERCF::Config::Get();
			if (!cfg.override_mode_strict || s_inERCFApply) {
				a_orig(a_proj, a_col);
				return;
			}

			if (a_proj->IsFlameProjectile() || a_proj->IsBeamProjectile() || a_proj->IsBarrierProjectile()) {
				a_orig(a_proj, a_col);
				return;
			}

			RE::Actor* victim = FindActorVictimFromCdCollector(a_col);
			if (!victim || victim->IsDead()) {
				a_orig(a_proj, a_col);
				return;
			}
			if (!VictimEligibleForDamageOverride(victim)) {
				a_orig(a_proj, a_col);
				return;
			}

			auto& rd = a_proj->GetProjectileRuntimeData();
			auto* aggressor = rd.shooter.get().get() ? rd.shooter.get().get()->As<RE::Actor>() : nullptr;
			if (!aggressor) {
				a_orig(a_proj, a_col);
				return;
			}

			const float enginePhys = EstimateProjectileVanillaPhysicalDamage(a_proj);
			if (enginePhys <= 1e-4f) {
				// Runtime often clears rd.weaponSource / rd.spell on MissileProjectile; HitEventHandler-style guess
				// from equipped weapon so ExtractHitSource sees staff formEnchanting + spell MGEFs when authored.
				RE::MagicItem* spellForHit = rd.spell;
				const RE::TESObjectWEAP* weaponForHit = rd.weaponSource;
				bool ercfElem = ProjectileHasErcfElementalDamage(spellForHit, weaponForHit);
				if (!ercfElem) {
					for (const bool lh : { false, true }) {
						if (const auto* form = aggressor->GetEquippedObject(lh)) {
							if (const auto* w = form->As<RE::TESObjectWEAP>()) {
								if (ProjectileHasErcfElementalDamage(spellForHit, w)) {
									weaponForHit = w;
									ercfElem = true;
									break;
								}
							}
						}
					}
				}
				if (ercfElem && !weaponForHit) {
					for (const bool lh : { false, true }) {
						if (const auto* form = aggressor->GetEquippedObject(lh)) {
							if (const auto* w = form->As<RE::TESObjectWEAP>()) {
								if (w->IsStaff()) {
									weaponForHit = w;
									break;
								}
							}
						}
					}
				}

				if (ercfElem) {
					const float spellVanillaScale =
						ComputeSpellStaffElementalVanillaScale(aggressor, weaponForHit, cfg, spellForHit);
					a_orig(a_proj, a_col);

					RE::HitData hit{};
					hit.aggressor = aggressor->GetHandle();
					hit.target = victim->GetHandle();
					hit.weapon = const_cast<RE::TESObjectWEAP*>(weaponForHit);
					hit.attackDataSpell = spellForHit ? spellForHit->As<RE::SpellItem>() : nullptr;
					hit.totalDamage = 0.0f;
					hit.physicalDamage = 0.0f;

					ApplyRangedSneakCritToProjectileHit(victim, aggressor, weaponForHit, hit);

					const float ercfDamage = ComputeRequirementDamageForVictim(
						victim,
						hit,
						0.0f,
						spellVanillaScale,
						spellForHit);
					if (ercfDamage <= 0.0f) {
						return;
					}
					if (auto* avOwner = victim->AsActorValueOwner()) {
						s_inERCFApply = true;
						avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -ercfDamage);
						s_inERCFApply = false;
					}
					if (cfg.override_debug_log) {
						LOG_INFO(
							"ERCF[OverrideProof]: spell missile={} victim={} aggressor={} spellVanillaScale={:.3f} ercf={:.3f}",
							a_proj->GetFormID(),
							victim->GetFormID(),
							aggressor->GetFormID(),
							spellVanillaScale,
							ercfDamage);
					}
					return;
				}
				a_orig(a_proj, a_col);
				return;
			}

			const float savedWeaponDamage = rd.weaponDamage;
			rd.weaponDamage = 0.0f;
			a_orig(a_proj, a_col);
			rd.weaponDamage = savedWeaponDamage;

			RE::HitData hit{};
			hit.aggressor = aggressor->GetHandle();
			hit.target = victim->GetHandle();
			hit.weapon = rd.weaponSource;
			hit.attackDataSpell = rd.spell ? rd.spell->As<RE::SpellItem>() : nullptr;
			const float ercfPhysRaw = ResolveProjectilePhysicalRawForRequirement(a_proj, enginePhys);
			hit.totalDamage = ercfPhysRaw;
			hit.physicalDamage = ercfPhysRaw;

			ApplyRangedSneakCritToProjectileHit(victim, aggressor, rd.weaponSource, hit);

			const float ercfDamage = ComputeRequirementDamageForVictim(victim, hit, ercfPhysRaw);
			if (ercfDamage <= 0.0f) {
				return;
			}
			if (auto* avOwner = victim->AsActorValueOwner()) {
				s_inERCFApply = true;
				avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -ercfDamage);
				s_inERCFApply = false;
			}

			if (cfg.override_debug_log) {
				LOG_INFO(
					"ERCF[OverrideProof]: projectile victim={} aggressor={} enginePhys={:.3f} ercfRawPhys={:.3f} "
					"sneak={} crit={} cm={:.3f} suppressed -> ercf={:.3f}",
					victim->GetFormID(),
					aggressor->GetFormID(),
					enginePhys,
					ercfPhysRaw,
					hit.flags.any(RE::HitData::Flag::kSneakAttack),
					hit.flags.any(RE::HitData::Flag::kCritical),
					hit.criticalDamageMult,
					ercfDamage);
			}
		}

		static inline REL::Relocation<ProjectileCollisionFn> s_origArrowProjectileCollision;
		static inline REL::Relocation<ProjectileCollisionFn> s_origMissileProjectileCollision;

		static void ArrowProjectileCollisionThunk(RE::Projectile* a_proj, RE::hkpAllCdPointCollector* a_col)
		{
			ProjectileCollisionCommon(a_proj, a_col, s_origArrowProjectileCollision);
		}

		static void MissileProjectileCollisionThunk(RE::Projectile* a_proj, RE::hkpAllCdPointCollector* a_col)
		{
			ProjectileCollisionCommon(a_proj, a_col, s_origMissileProjectileCollision);
		}

		class Hook_ProjectileCollision
		{
		public:
			static void Install()
			{
				REL::Relocation<std::uintptr_t> arrowVtbl{ RE::VTABLE_ArrowProjectile[0] };
				REL::Relocation<std::uintptr_t> missileVtbl{ RE::VTABLE_MissileProjectile[0] };
				s_origArrowProjectileCollision = arrowVtbl.write_vfunc(
					kProjectileCollisionVfuncIndex,
					ArrowProjectileCollisionThunk);
				s_origMissileProjectileCollision = missileVtbl.write_vfunc(
					kProjectileCollisionVfuncIndex,
					MissileProjectileCollisionThunk);
				LOG_INFO("ERCF: OverrideDamageHook installed (arrow + missile projectile collision)");
			}
		};

		class Hook_PreCommitMeleeHit
		{
		public:
			static void Install()
			{
				auto& trampoline = SKSE::GetTrampoline();
				constexpr std::size_t size_per_hook = 14;
				constexpr std::size_t num_hooks = 1;
				trampoline.create(size_per_hook * num_hooks);

				// Reference pattern: Parry-for-all Hook_OnMeleeHit::processHit.
				// NOTE: relocation IDs/offsets must be validated on your runtime targets.
				REL::Relocation<std::uintptr_t> hook{ RELOCATION_ID(37673, 38627) };
				_ProcessHit = trampoline.write_call<5>(
					hook.address() + REL::Relocate(0x3C0, 0x4A8),
					ProcessHit);

				LOG_INFO("ERCF: OverrideDamageHook installed (pre-commit melee hit)");
			}

		private:
			static void ProcessHit(RE::Actor* a_victim, RE::HitData& a_hitData)
			{
				if (!a_victim) {
					return _ProcessHit(a_victim, a_hitData);
				}

				const auto& cfg = ERCF::Config::Get();
				if (!cfg.override_mode_strict || s_inERCFApply) {
					return _ProcessHit(a_victim, a_hitData);
				}

				auto* aggressor = a_hitData.aggressor.get().get();
				if (!aggressor || a_victim->IsDead()) {
					return _ProcessHit(a_victim, a_hitData);
				}
				if (!VictimEligibleForDamageOverride(a_victim)) {
					return _ProcessHit(a_victim, a_hitData);
				}

				// Blocked melee (requirement.md §5): ERCF stamina damage only while victim has stamina; no ERCF HP.
				// Guard break (stamina ~0) falls through to normal strict HP path below.
				const bool blockedHit = a_hitData.flags.any(RE::HitData::Flag::kBlocked) ||
					a_hitData.flags.any(RE::HitData::Flag::kBlockWithWeapon);
				if (blockedHit) {
					float curStamina = 0.0f;
					if (auto* avOwner = a_victim->AsActorValueOwner()) {
						curStamina = avOwner->GetActorValue(RE::ActorValue::kStamina);
					}
					if (curStamina > 1e-3f) {
						const float atkW = ResolveAggressorWeaponWeightForBlock(a_hitData);
						const bool power = a_hitData.flags.any(RE::HitData::Flag::kPowerAttack);
						const bool sprint = AggressorIsSprinting(aggressor);
						const float incoming = Math::BlockStaminaIncoming(atkW, power, sprint);
						const float guard = ResolveBlockGuardPoint(a_victim, a_hitData);
						const float recv = Math::BlockStaminaReceived(incoming, guard);

						a_hitData.totalDamage = 0.0f;
						a_hitData.physicalDamage = 0.0f;
						_ProcessHit(a_victim, a_hitData);

						if (recv > 1e-4f) {
							if (auto* avOwner = a_victim->AsActorValueOwner()) {
								s_inERCFApply = true;
								avOwner->RestoreActorValue(
									RE::ACTOR_VALUE_MODIFIER::kDamage,
									RE::ActorValue::kStamina,
									-recv);
								s_inERCFApply = false;
							}
						}
						return;
					}
				}

				// Suppress vanilla HP for this hit, then apply a single ERCF HP delta.
				// Physical raw: weapon GetAttackDamage() for humanoids with a weapon; else CalcUnarmedDamage(); if
				// that is ~0, fall back to vanilla rolled HitData (edge cases). Clear HitData physical after
				// _ProcessHit so ComputeRequirementDamageForVictim uses meleePhysIn, not rolled HitData.physicalDamage.
				const float vanillaPhysFallback =
					(a_hitData.physicalDamage > 1e-4f) ? a_hitData.physicalDamage : a_hitData.totalDamage;
				const float meleePhysIn =
					ResolveMeleePhysicalRawForRequirement(aggressor, a_hitData, vanillaPhysFallback);
				a_hitData.totalDamage = 0.0f;
				_ProcessHit(a_victim, a_hitData);
				a_hitData.physicalDamage = 0.0f;

				const float ercfDamage = ComputeRequirementDamageForVictim(a_victim, a_hitData, meleePhysIn);
				if (ercfDamage <= 0.0f) {
					return;
				}
				if (auto* avOwner = a_victim->AsActorValueOwner()) {
					s_inERCFApply = true;
					avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -ercfDamage);
					s_inERCFApply = false;
				}
			}

			static inline REL::Relocation<decltype(ProcessHit)> _ProcessHit;
		};
	}

	void Install()
	{
		Hook_PreCommitMeleeHit::Install();
		Hook_ProjectileCollision::Install();
	}
}

