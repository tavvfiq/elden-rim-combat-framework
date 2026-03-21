#include "pch.h"

#include "HitEventHandler.h"

#include <array>
#include <chrono>

#include "CombatMath.h"
#include "EspRouting.h"
#include "Messaging.h"
#include "Config.h"
#include "PrismaUIMeters.h"
#include "Log.h"

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
			struct MeterState
			{
				float poisonMeter = 0.0f;
				float bleedMeter = 0.0f;

				// steady_clock seconds
				double poisonLastBuildupSec = 0.0;
				double bleedLastBuildupSec = 0.0;
			};

			std::unordered_map<RE::FormID, MeterState> s_meterStates;
			std::mutex s_meterMutex;

			[[nodiscard]] double NowSeconds()
			{
				using clock = std::chrono::steady_clock;
				using sec = std::chrono::duration<double>;
				return std::chrono::duration_cast<sec>(clock::now().time_since_epoch()).count();
			}

			void ApplyDecayAndAccumulate(
				float& a_meter,
				double& a_lastBuildupSec,
				float a_rawPayload,
				float a_resValue,
				const Config::Values& a_cfg,
				double a_nowSec)
			{
				// Apply decay based on time since last buildup.
				const double timeSince = a_nowSec - a_lastBuildupSec;
				const double dtAfterDelay = std::max(0.0, timeSince - a_cfg.status_decay_delay_seconds);
				a_meter = Math::DecayMeter(
					a_meter,
					dtAfterDelay,
					timeSince,
					a_cfg.status_decay_delay_seconds,
					a_cfg.status_decay_rate);

				// Accumulate.
				const float effectivePayload = Math::EffectivePayload(a_rawPayload, a_resValue, a_cfg.k_resist);
				a_meter = Math::AccumulateMeter(a_meter, effectivePayload);
				a_lastBuildupSec = a_nowSec;
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

			RE::HitData* hitData = nullptr;
			if (auto* mh = target->GetMiddleHighProcess()) {
				hitData = mh->lastHitData;
			}

			// Only react to hits that actually dealt HP damage (vanilla HitData snapshot).
			if (hitData && hitData->totalDamage <= 0.0f) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const double nowSec = NowSeconds();
			const auto cfg = ERCF::Config::Get();

			const RE::InventoryEntryData* weaponEntry = attacker->GetAttackingWeapon();
			RE::MagicItem* hitSpell = hitData ? hitData->attackDataSpell : nullptr;

			Esp::StatusBuildupCoefficients payload{};
			Esp::ElementalHitComponents elemental{};
			Esp::ExtractHitSourceFromWeaponAndSpell(weaponEntry, hitSpell, payload, elemental);

			bool anyElemental = false;
			for (std::size_t i = static_cast<std::size_t>(Esp::DamageTypeId::Magic); i < Esp::kDamageTypeCount; ++i) {
				if (elemental.attack[i] > 0.0f) {
					anyElemental = true;
					break;
				}
			}

			if (payload.poisonPayload <= 0.0f && payload.bleedPayload <= 0.0f && !anyElemental) {
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
				const bool hasWench = weaponEntry && weaponEntry->GetEnchantment();
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

			if (anyElemental) {
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
					if (dmg > 0.0f) {
						target->DamageActorValue(RE::ActorValue::kHealth, dmg);
					}
				}
			}

			if (payload.poisonPayload <= 0.0f && payload.bleedPayload <= 0.0f) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const bool playerTarget = target->IsPlayerRef();
			float poisonAfter = 0.0f;
			float bleedAfter = 0.0f;

			std::lock_guard<std::mutex> lock{s_meterMutex};
			auto& state = s_meterStates[target->GetFormID()];

			// Initialize timestamps so first decay step doesn't instantly wipe.
			if (state.poisonLastBuildupSec == 0.0) state.poisonLastBuildupSec = nowSec;
			if (state.bleedLastBuildupSec == 0.0) state.bleedLastBuildupSec = nowSec;

			// Poison meter update
			if (payload.poisonPayload > 0.0f) {
				const float before = state.poisonMeter;
				ApplyDecayAndAccumulate(
					state.poisonMeter,
					state.poisonLastBuildupSec,
					payload.poisonPayload,
					resist.immunityResValue,
					cfg,
					nowSec);

				if (Math::MeterPops(state.poisonMeter)) {
					// Reset meter on pop.
					state.poisonMeter = 0.0f;
					state.poisonLastBuildupSec = nowSec;

					if (cfg.debug_hit_events) {
						ERCFLog::LineF(
							"ERCF [Hit] StatusProc emit POISON attacker=%08X target=%08X meterBefore=%.3f payload=%.3f",
							attacker->GetFormID(),
							target->GetFormID(),
							before,
							payload.poisonPayload);
					}

					StatusProcMessage msg{};
					msg.attackerFormId = attacker->GetFormID();
					msg.targetFormId = target->GetFormID();
					msg.statusId = Status_Poison;
					msg.band = Band_Immunity;
					msg.meterBeforePop = before;
					msg.payloadAtPop = payload.poisonPayload;
					EmitStatusProc(msg);
				}
			}

			// Bleed meter update
			if (payload.bleedPayload > 0.0f) {
				const float before = state.bleedMeter;
				ApplyDecayAndAccumulate(
					state.bleedMeter,
					state.bleedLastBuildupSec,
					payload.bleedPayload,
					resist.robustnessResValue,
					cfg,
					nowSec);

				if (Math::MeterPops(state.bleedMeter)) {
					state.bleedMeter = 0.0f;
					state.bleedLastBuildupSec = nowSec;

					if (cfg.debug_hit_events) {
						ERCFLog::LineF(
							"ERCF [Hit] StatusProc emit BLEED attacker=%08X target=%08X meterBefore=%.3f payload=%.3f",
							attacker->GetFormID(),
							target->GetFormID(),
							before,
							payload.bleedPayload);
					}

					StatusProcMessage msg{};
					msg.attackerFormId = attacker->GetFormID();
					msg.targetFormId = target->GetFormID();
					msg.statusId = Status_Bleed;
					msg.band = Band_Robustness;
					msg.meterBeforePop = before;
					msg.payloadAtPop = payload.bleedPayload;
					EmitStatusProc(msg);
				}
			}

			poisonAfter = state.poisonMeter;
			bleedAfter = state.bleedMeter;

			if (playerTarget) {
				ERCFLog::LineF("ERCF: Player meters poison=%.2f bleed=%.2f", poisonAfter, bleedAfter);
			} else if (cfg.debug_hit_events && (payload.poisonPayload > 0.0f || payload.bleedPayload > 0.0f)) {
				ERCFLog::LineF(
					"ERCF [Hit] NPC target meters poison=%.2f bleed=%.2f (target=%08X)",
					poisonAfter,
					bleedAfter,
					target->GetFormID());
			}

			// Update UI outside the meter lock (interop can touch other subsystems).
			if (playerTarget) {
				Prisma::UpdateMeters(poisonAfter, bleedAfter);
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	}
}

