#include "pch.h"

#include "HitEventHandler.h"

#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>

#include "CombatMath.h"
#include "Config.h"
#include "EspRouting.h"
#include "Log.h"
#include "StatusEffectManager.h"

#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/A/Actor.h"
#include "RE/H/HitData.h"
#include "RE/T/TESForm.h"

namespace ERCF
{
	namespace Runtime
	{
		namespace
		{
			[[nodiscard]] const char* DescribeWeaponEntryTrace(Esp::HitMagicTrace::WeaponEntry a_k)
			{
				using WE = Esp::HitMagicTrace::WeaponEntry;
				switch (a_k) {
				case WE::AttackingWeapon:
					return "attackingWeapon";
				case WE::EquippedSlotMatch:
					return "equippedSlotMatch";
				case WE::None:
					return "none";
				}
				return "none";
			}

			[[nodiscard]] const char* DescribeWeaponEnchantTrace(Esp::HitMagicTrace::WeaponEnchant a_k)
			{
				using WEn = Esp::HitMagicTrace::WeaponEnchant;
				switch (a_k) {
				case WEn::InstanceOnEntry:
					return "instanceOnEntry";
				case WEn::BoundWeaponEITM:
					return "boundWeaponEITM";
				case WEn::HitDataWeaponFormEITM:
					return "hitDataWeaponFormEITM_fallback";
				case WEn::None:
					return "none";
				}
				return "none";
			}

			// Skyrim often emits paired TESHitEvents per swing with totalDamage==0; both would apply full buildup.
			std::mutex s_zeroDamageBuildupDedupeMutex;
			std::unordered_map<std::uint64_t, double> s_zeroDamageBuildupLastSec;
			constexpr double kZeroDamageBuildupDedupeWindowSec = 0.065;

			[[nodiscard]] double NowSeconds()
			{
				using clock = std::chrono::steady_clock;
				using sec = std::chrono::duration<double>;
				return std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();
			}
		}

		HitEventHandler* HitEventHandler::GetSingleton()
		{
			static HitEventHandler singleton;
			return std::addressof(singleton);
		}

		void HitEventHandler::Register()
		{
			auto* scripts = RE::ScriptEventSourceHolder::GetSingleton();
			if (!scripts) {
				ERCFLog::Line("ERCF: ScriptEventSourceHolder missing; TESHitEvent sink not registered");
				return;
			}

			scripts->AddEventSink(GetSingleton());
			ERCFLog::Line("ERCF: TESHitEvent sink registered");
		}

		RE::BSEventNotifyControl HitEventHandler::ProcessEvent(const RE::TESHitEvent* a_event,
			RE::BSTEventSource<RE::TESHitEvent>*)
		{
			if (!a_event || !a_event->target || !a_event->cause) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Skip blocked hits (parry/block should not progress buildup).
			if (a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* attacker = a_event->cause->As<RE::Actor>();
			auto* target = a_event->target->As<RE::Actor>();

			if (!attacker || !target) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (target->IsDead()) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Bash attacks reuse weapon enchant resolution but should not drive status buildup (and often double-fire with swings).
			if (a_event->flags.any(RE::TESHitEvent::Flag::kBashAttack)) {
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::HitData* hitData = nullptr;
			if (auto* mh = target->GetMiddleHighProcess()) {
				hitData = mh->lastHitData;
			}

			const double nowSec = NowSeconds();
			const auto cfg = ERCF::Config::Get();

			// Duplicate / sentinel hit phases (commonly totalDamage == -1) must not each apply decay+buildup.
			if (hitData && hitData->totalDamage < 0.0f) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Prefer attacking weapon; NPCs often need equipped entry matched to HitData::weapon.
			Esp::HitMagicTrace magicTrace{};
			const RE::InventoryEntryData* weaponEntry = attacker->GetAttackingWeapon();
			if (weaponEntry) {
				magicTrace.weaponEntryKind = Esp::HitMagicTrace::WeaponEntry::AttackingWeapon;
			} else if (hitData && hitData->weapon) {
				const auto* hitWeap = hitData->weapon;
				if (attacker->GetEquippedObject(false) == static_cast<const RE::TESForm*>(hitWeap)) {
					weaponEntry = attacker->GetEquippedEntryData(false);
					if (weaponEntry) {
						magicTrace.weaponEntryKind = Esp::HitMagicTrace::WeaponEntry::EquippedSlotMatch;
					}
				} else if (attacker->GetEquippedObject(true) == static_cast<const RE::TESForm*>(hitWeap)) {
					weaponEntry = attacker->GetEquippedEntryData(true);
					if (weaponEntry) {
						magicTrace.weaponEntryKind = Esp::HitMagicTrace::WeaponEntry::EquippedSlotMatch;
					}
				}
			}
			const RE::TESObjectWEAP* weaponFormFallback = (hitData && hitData->weapon) ? hitData->weapon : nullptr;
			RE::MagicItem* hitSpell = hitData ? hitData->attackDataSpell : nullptr;

			Esp::StatusBuildupCoefficients payload{};
			Esp::ElementalHitComponents elemental{};
			Esp::HitMagicTrace* tracePtr = cfg.debug_hit_events ? &magicTrace : nullptr;
			Esp::ExtractHitSourceFromWeaponAndSpell(weaponEntry, hitSpell, payload, elemental, weaponFormFallback, tracePtr);

			bool anyElemental = false;
			for (std::size_t i = static_cast<std::size_t>(Esp::DamageTypeId::Magic); i < Esp::kDamageTypeCount; ++i) {
				if (elemental.attack[i] > 0.0f) {
					anyElemental = true;
					break;
				}
			}

			if (payload.poisonPayload <= 0.0f &&
				payload.bleedPayload <= 0.0f &&
				payload.rotPayload <= 0.0f &&
				payload.frostbitePayload <= 0.0f &&
				payload.sleepPayload <= 0.0f &&
				payload.madnessPayload <= 0.0f &&
				!anyElemental) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// If HitData says 0 damage, still allow poison/bleed buildup + HUD; elemental HP extra only on real hits.
			const bool vanillaDamagingHit = !hitData || hitData->totalDamage > 0.0f;
			if (hitData && hitData->totalDamage <= 0.0f &&
				payload.poisonPayload <= 0.0f &&
				payload.bleedPayload <= 0.0f &&
				payload.rotPayload <= 0.0f &&
				payload.frostbitePayload <= 0.0f &&
				payload.sleepPayload <= 0.0f &&
				payload.madnessPayload <= 0.0f) {
				return RE::BSEventNotifyControl::kContinue;
			}

			Esp::MitigationCoefficients mitigation{};
			Esp::StatusResistanceCoefficients resist{};
			Esp::ExtractFromWornArmor(target, cfg.armor_rating_defense_scale, mitigation, resist);
			Esp::MergeMitigationFromActiveActorEffects(target, mitigation);
			Esp::MergeStatusResistanceFromActiveActorEffects(target, resist);

			std::array<float, Esp::kDamageTypeCount> takenMult{};
			Esp::ExtractTakenDamageMultipliers(target, cfg, takenMult);

			if (cfg.debug_hit_events) {
				const float td = hitData ? hitData->totalDamage : -1.0f;
				const float pd = hitData ? hitData->physicalDamage : -1.0f;
				const bool hasWench = (weaponEntry && weaponEntry->GetEnchantment()) ||
					(weaponFormFallback && weaponFormFallback->formEnchanting);
				ERCFLog::LineF(
					"ERCF [Hit] attacker=%08X target=%08X vanilla totalDmg=%.2f physDmg=%.2f wpnEnch=%d spell=%p "
					"buildup poison=%.3f bleed=%.3f elem(Ma/Fi/Li/Ho)=%.2f,%.2f,%.2f,%.2f",
					attacker->GetFormID(),
					target->GetFormID(),
					td,
					pd,
					hasWench ? 1 : 0,
					static_cast<void*>(hitSpell),
					payload.poisonPayload,
					payload.bleedPayload,
					elemental.attack[4],
					elemental.attack[5],
					elemental.attack[6],
					elemental.attack[7]);

				const bool hitWeapNoInvEntry =
					weaponFormFallback != nullptr &&
					magicTrace.weaponEntryKind == Esp::HitMagicTrace::WeaponEntry::None;
				const bool inconsistentNoMagicSource =
					(payload.poisonPayload > 0.0f || payload.bleedPayload > 0.0f || anyElemental) &&
					magicTrace.weaponEnchantKind == Esp::HitMagicTrace::WeaponEnchant::None &&
					!magicTrace.accumulatedSeparateHitSpell;
				const bool usedFallbackEITM =
					magicTrace.weaponEnchantKind == Esp::HitMagicTrace::WeaponEnchant::HitDataWeaponFormEITM;
				ERCFLog::LineF(
					"ERCF [Hit] magicTrace wEntry=%s wEnchant=%s hitSpellExtra=%s vanillaDamagingHit=%s "
					"flags=%s%s%ssummary=%s",
					DescribeWeaponEntryTrace(magicTrace.weaponEntryKind),
					DescribeWeaponEnchantTrace(magicTrace.weaponEnchantKind),
					magicTrace.accumulatedSeparateHitSpell ? "yes" : "no",
					vanillaDamagingHit ? "yes" : "no",
					hitWeapNoInvEntry ? "hitWeapNoInvEntry " : "",
					usedFallbackEITM ? "usedFallbackEITM " : "",
					inconsistentNoMagicSource ? "INCONSISTENT_noEnchantSource " : "",
					(hitWeapNoInvEntry || usedFallbackEITM || inconsistentNoMagicSource) ? "issues" : "ok");
				ERCFLog::LineF(
					"ERCF [Hit] target resist immunity=%.3f robustness=%.3f "
					"def(std/str/sla/pie)=%.2f,%.2f,%.2f,%.2f def(mag/fir/lig/hol)=%.2f,%.2f,%.2f,%.2f",
					resist.immunityResValue,
					resist.robustnessResValue,
					mitigation.defense[0],
					mitigation.defense[1],
					mitigation.defense[2],
					mitigation.defense[3],
					mitigation.defense[4],
					mitigation.defense[5],
					mitigation.defense[6],
					mitigation.defense[7]);
				ERCFLog::LineF(
					"ERCF [Hit] takenMult(std/str/sla/pie)=%.3f,%.3f,%.3f,%.3f takenMult(ma/fi/li/ho)=%.3f,%.3f,%.3f,%.3f",
					takenMult[0],
					takenMult[1],
					takenMult[2],
					takenMult[3],
					takenMult[4],
					takenMult[5],
					takenMult[6],
					takenMult[7]);
			}

			// In strict override mode, on-hit HP is owned by OverrideDamageHook.
			// TESHitEvent path remains for status buildup/procs only.
			if (!cfg.override_mode_strict && vanillaDamagingHit && anyElemental) {
				for (std::size_t i = static_cast<std::size_t>(Esp::DamageTypeId::Magic); i < Esp::kDamageTypeCount; ++i) {
					const float atk = elemental.attack[i] * cfg.elemental_enchant_damage_scale;
					if (atk <= 0.0f) {
						continue;
					}
					const float postMit = Math::DamageAfterDefenseAndAbsorption(
						atk,
						mitigation.defense[i],
						mitigation.absorptionFractions[i],
						cfg);
					const float dmg = Math::ApplyTakenDamageMultiplier(postMit, takenMult[i]);
					if (cfg.debug_hit_events) {
						ERCFLog::LineF(
							"ERCF [Hit] elem typeIdx=%u atk=%.3f postMitig=%.3f absCount=%u takenMult=%.3f applyHP=%.3f",
							static_cast<unsigned>(i),
							atk,
							postMit,
							static_cast<unsigned>(mitigation.absorptionFractions[i].size()),
							takenMult[i],
							dmg);
					}
					// Only finite, strictly positive damage; negative/zero avoids accidentally healing
					// (DamageActorValue uses positive as damage and keeps HUD/death state in sync).
					if (std::isfinite(dmg) && dmg > 0.0f) {
						if (auto* avOwner = target->AsActorValueOwner()) {
							avOwner->ModActorValue(RE::ActorValue::kHealth, -dmg);
						}
					}
				}
			}

			const bool playerTarget = target->IsPlayerRef();
			const bool isBoss = target->HasKeywordString("ActorTypeBoss");
			float frostPayload = payload.frostbitePayload;
			if (frostPayload > 0.0f) {
				const float fireMag = elemental.attack[static_cast<std::size_t>(Esp::DamageTypeId::Fire)];
				if (fireMag > 0.0f) {
					frostPayload = (std::max)(0.0f, frostPayload - fireMag);
				}
			}
			const bool willApplyStatus =
				payload.poisonPayload > 0.0f || payload.bleedPayload > 0.0f || payload.rotPayload > 0.0f ||
				frostPayload > 0.0f || (!isBoss && payload.sleepPayload > 0.0f) ||
				(payload.madnessPayload > 0.0f && playerTarget);

			if (!willApplyStatus) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Status buildup uses real HitData: early-phase events often have no lastHitData yet (log shows totalDmg=-1);
			// processing those duplicates the following real hit and makes meters jumpy.
			if (!hitData || hitData->totalDamage < 0.0f) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Drop duplicate zero-damage phases for the same attacker→target within one swing window.
			if (hitData->totalDamage == 0.0f) {
				const std::uint64_t dedupeKey =
					(static_cast<std::uint64_t>(static_cast<std::uint32_t>(attacker->GetFormID())) << 32u) |
					static_cast<std::uint32_t>(target->GetFormID());
				std::lock_guard<std::mutex> dedupeLock{s_zeroDamageBuildupDedupeMutex};
				const auto it = s_zeroDamageBuildupLastSec.find(dedupeKey);
				if (it != s_zeroDamageBuildupLastSec.end() &&
					(nowSec - it->second) < kZeroDamageBuildupDedupeWindowSec) {
					return RE::BSEventNotifyControl::kContinue;
				}
				s_zeroDamageBuildupLastSec[dedupeKey] = nowSec;
				if (s_zeroDamageBuildupLastSec.size() > 400u) {
					s_zeroDamageBuildupLastSec.clear();
				}
			}

			const auto th = StatusEffects::ComputeThresholds(
				target->GetFormID(),
				target,
				playerTarget,
				resist);

			const std::uint32_t atkF = attacker->GetFormID();
			const std::uint32_t tgtF = target->GetFormID();
			using T = StatusEffects::Type;

			StatusEffects::ProcRequirementSnapshot req{};
			StatusEffects::FillProcRequirementSnapshot(attacker, target, req);

			bool anyProc = false;
			if (payload.poisonPayload > 0.0f) {
				StatusEffects::ProcStatusEffect(StatusEffects::ProcInput{
					atkF,
					tgtF,
					T::Poison,
					payload.poisonPayload,
					th.immunity,
					req,
					isBoss});
				anyProc = true;
			}
			if (payload.rotPayload > 0.0f) {
				StatusEffects::ProcStatusEffect(StatusEffects::ProcInput{
					atkF,
					tgtF,
					T::Rot,
					payload.rotPayload,
					th.immunity,
					req,
					isBoss});
				anyProc = true;
			}
			if (payload.bleedPayload > 0.0f) {
				StatusEffects::ProcStatusEffect(StatusEffects::ProcInput{
					atkF,
					tgtF,
					T::Bleed,
					payload.bleedPayload,
					th.robustness,
					req,
					isBoss});
				anyProc = true;
			}
			if (frostPayload > 0.0f) {
				StatusEffects::ProcStatusEffect(StatusEffects::ProcInput{
					atkF,
					tgtF,
					T::Frostbite,
					frostPayload,
					th.robustness,
					req,
					isBoss});
				anyProc = true;
			}
			if (!isBoss && payload.sleepPayload > 0.0f) {
				StatusEffects::ProcStatusEffect(StatusEffects::ProcInput{
					atkF,
					tgtF,
					T::Sleep,
					payload.sleepPayload,
					th.focus,
					req,
					isBoss});
				anyProc = true;
			}
			if (payload.madnessPayload > 0.0f && playerTarget) {
				StatusEffects::ProcStatusEffect(StatusEffects::ProcInput{
					atkF,
					tgtF,
					T::Madness,
					payload.madnessPayload,
					th.madness,
					req,
					isBoss});
				anyProc = true;
			}

			const bool npcMetersLog = cfg.debug_hit_events && !playerTarget &&
				(payload.poisonPayload > 0.0f || payload.bleedPayload > 0.0f || payload.rotPayload > 0.0f ||
					frostPayload > 0.0f || (!isBoss && payload.sleepPayload > 0.0f));
			if (npcMetersLog) {
				StatusEffects::PlayerMetersHudSnapshot m{};
				(void)StatusEffects::TryGetMeterSnapshotForHud(tgtF, m);
				ERCFLog::LineF(
					"ERCF [Hit] NPC target meters poison=%.2f bleed=%.2f rot=%.2f frost=%.2f sleep=%.2f (target=%08X)",
					m.poison,
					m.bleed,
					m.rot,
					m.frostbite,
					m.sleep,
					tgtF);
			}

			if (anyProc) {
				StatusEffects::RequestDeferredFlush();
			}

			return RE::BSEventNotifyControl::kContinue;
		}

	bool TryGetPlayerMetersHudSnapshot(StatusEffects::PlayerMetersHudSnapshot& a_out)
	{
		return StatusEffects::TryGetPlayerMetersHudSnapshot(a_out);
	}

	bool TryGetPlayerMeterSnapshotForHud(float& poisonOut, float& bleedOut)
	{
		return StatusEffects::TryGetPlayerMeterSnapshotForHud(poisonOut, bleedOut);
	}
	}
}

