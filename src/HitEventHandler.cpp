#include "pch.h"

#include "HitEventHandler.h"

#include <chrono>

#include "CombatMath.h"
#include "EspRouting.h"
#include "Messaging.h"
#include "Config.h"
#include "PrismaUIMeters.h"
#include "Log.h"

#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/A/Actor.h"
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

			// Minimal HitData usage (plan alignment):
			// only progress buildup if the hit actually dealt HP damage.
			if (auto* mh = target->GetMiddleHighProcess()) {
				if (auto* hitData = mh->lastHitData; hitData && hitData->totalDamage <= 0.0f) {
					return RE::BSEventNotifyControl::kContinue;
				}
			}

			const double nowSec = NowSeconds();
			const auto cfg = ERCF::Config::Get();

			// Extract v1 status payloads and resist values from active effects.
			// Note: later we will refine payload sourcing to be per-spell/per-weapon rather than actor-wide.
			Esp::StatusBuildupCoefficients payload{};
			Esp::StatusResistanceCoefficients resist{};

			Esp::ExtractStatusPayloadFromActiveEffects(attacker, payload);
			Esp::ExtractStatusResistancesFromActiveEffects(target, resist);

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
			}

			// Update UI outside the meter lock (interop can touch other subsystems).
			if (playerTarget) {
				Prisma::UpdateMeters(poisonAfter, bleedAfter);
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	}
}

