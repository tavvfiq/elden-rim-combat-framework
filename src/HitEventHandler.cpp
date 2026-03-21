#include "pch.h"

#include "HitEventHandler.h"

#include <chrono>
#include <atomic>

#include "CombatMath.h"
#include "EspRouting.h"
#include "Messaging.h"
#include "Config.h"
#include "PrismaUIMeters.h"
#include "Log.h"

#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/A/Actor.h"
#include "RE/C/Console.h"
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

			struct ResistanceCache
			{
				float immunityResValue = 0.0f;
				float robustnessResValue = 0.0f;
				// steady_clock seconds
				double lastUpdateSec = 0.0;
			};

			std::unordered_map<RE::FormID, ResistanceCache> s_resistCache;
			std::mutex s_resistCacheMutex;

			std::atomic<std::uint64_t> s_hitEventCounter{ 0 };

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

			[[nodiscard]] Esp::StatusResistanceCoefficients GetCachedResistances(
				const RE::Actor* a_target,
				double a_nowSec,
				const Config::Values& a_cfg)
			{
				Esp::StatusResistanceCoefficients out{};
				if (!a_target) {
					return out;
				}

				const auto formId = a_target->GetFormID();

				{
					std::lock_guard<std::mutex> lock{s_resistCacheMutex};
					auto& cached = s_resistCache[formId];
					const bool cacheExpired =
						cached.lastUpdateSec <= 0.0 ||
						(a_nowSec - cached.lastUpdateSec) > a_cfg.status_resist_cache_ttl_seconds;

					if (!cacheExpired) {
						out.immunityResValue = cached.immunityResValue;
						out.robustnessResValue = cached.robustnessResValue;
						return out;
					}
				}

				// Recompute outside the cache mutex (avoid holding lock while walking effects).
				Esp::StatusResistanceCoefficients computed{};
				Esp::ExtractStatusResistancesFromActiveEffects(a_target, computed);

				{
					std::lock_guard<std::mutex> lock{s_resistCacheMutex};
					auto& cached = s_resistCache[formId];
					cached.immunityResValue = computed.immunityResValue;
					cached.robustnessResValue = computed.robustnessResValue;
					cached.lastUpdateSec = a_nowSec;
				}

				return computed;
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

			// Minimal HitData usage:
			// only progress buildup if the hit actually dealt damage to the target.
			RE::HitData* hitData = nullptr;
			if (auto* mh = target->GetMiddleHighProcess()) {
				hitData = mh->lastHitData;
				if (hitData && hitData->totalDamage <= 0.0f) {
					return RE::BSEventNotifyControl::kContinue;
				}
			}
			if (!hitData) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const double nowSec = NowSeconds();
			const auto cfg = ERCF::Config::Get();

			// Extract v1 status payloads and resist values.
			// Note: later we will refine payload sourcing to be per-spell/per-weapon rather than actor-wide.
			Esp::StatusBuildupCoefficients payload{};
			Esp::StatusResistanceCoefficients resist{};

			// vNext: meter buildup is sourced from the attacker's weapon enchant.
			Esp::ExtractStatusPayloadFromWeaponEnchant(hitData, payload);
			// Middle-ground: cache summed resist values per actor (TTL).
			resist = GetCachedResistances(target, nowSec, cfg);

			// If debug hit logging is enabled, we still want to log hits
			// even when they don't carry Poison/Bleed payload.
			std::uint32_t debugEveryN = 1;
			std::uint64_t hitIndex = 0;
			bool shouldLogHit = false;
			bool consoleSelectedMatchesTarget = false;
			RE::ObjectRefHandle consoleSelectedHandle{};
			RE::FormID consoleSelectedFormId = 0;
			if (cfg.debug_hit_log_enabled) {
				debugEveryN = std::max<std::uint32_t>(1, cfg.debug_hit_log_every_n);
				hitIndex = s_hitEventCounter.fetch_add(1, std::memory_order_relaxed) + 1;
				shouldLogHit = (hitIndex % static_cast<std::uint64_t>(debugEveryN)) == 0;

				if (auto selectedRef = RE::Console::GetSelectedRef(); selectedRef) {
					consoleSelectedFormId = selectedRef->GetFormID();
				}

				// `GetSelectedRef()` can be null for some selection cases (notably selecting the player),
				// so compare using ref handles instead of pointers/formIDs.
				consoleSelectedHandle = RE::Console::GetSelectedRefHandle();
				consoleSelectedMatchesTarget = consoleSelectedHandle == target->GetHandle();
			}

			const bool allowLogEvenIfSelectionMismatch =
				!cfg.debug_hit_log_require_console_target_match || cfg.debug_hit_log_log_selection_state;

			if (payload.poisonPayload <= 0.0f && payload.bleedPayload <= 0.0f) {
				if (shouldLogHit && (consoleSelectedMatchesTarget || allowLogEvenIfSelectionMismatch)) {
					const auto targetId = target->GetFormID();
					float poisonNow = 0.0f;
					float bleedNow = 0.0f;

					{
						std::lock_guard<std::mutex> lock{s_meterMutex};
						auto& state = s_meterStates[targetId];
						poisonNow = state.poisonMeter;
						bleedNow = state.bleedMeter;
					}

					ERCFLog::LineF(
						"ERCF DBG Hit #%llu sel=%u(%s) attacker=%u target=%u hitTotal=%.3f hitPhys=%.3f payload(poison=%.3f bleed=%.3f) resist(imm=%.3f rob=%.3f) meter(before: p=%.2f b=%.2f after: p=%.2f b=%.2f) pop(poison=%u bleed=%u)",
						static_cast<unsigned long long>(hitIndex),
						static_cast<std::uint32_t>(consoleSelectedFormId),
						consoleSelectedMatchesTarget ? "match" : "mismatch",
						attacker->GetFormID(),
						targetId,
						static_cast<float>(hitData->totalDamage),
						static_cast<float>(hitData->physicalDamage),
						payload.poisonPayload,
						payload.bleedPayload,
						static_cast<float>(resist.immunityResValue),
						static_cast<float>(resist.robustnessResValue),
						poisonNow,
						bleedNow,
						poisonNow,
						bleedNow,
						0u,
						0u
					);
				}

				return RE::BSEventNotifyControl::kContinue;
			}

			const bool playerTarget = target->IsPlayerRef();
			float poisonAfter = 0.0f;
			float bleedAfter = 0.0f;
			bool poisonPopped = false;
			bool bleedPopped = false;
			float poisonBefore = 0.0f;
			float bleedBefore = 0.0f;
			double hitTotalDamage = 0.0;
			double hitPhysicalDamage = 0.0;
			double resistImmunity = 0.0;
			double resistRobustness = 0.0;

			// Capture values for debug logging (avoid logging while holding the meter lock).
			hitTotalDamage = hitData->totalDamage;
			hitPhysicalDamage = hitData->physicalDamage;
			resistImmunity = resist.immunityResValue;
			resistRobustness = resist.robustnessResValue;

			{
				std::lock_guard<std::mutex> lock{s_meterMutex};
				auto& state = s_meterStates[target->GetFormID()];

				// Initialize timestamps so first decay step doesn't instantly wipe.
				if (state.poisonLastBuildupSec == 0.0) state.poisonLastBuildupSec = nowSec;
				if (state.bleedLastBuildupSec == 0.0) state.bleedLastBuildupSec = nowSec;

				// Poison meter update
				if (payload.poisonPayload > 0.0f) {
					poisonBefore = state.poisonMeter;
					ApplyDecayAndAccumulate(
						state.poisonMeter,
						state.poisonLastBuildupSec,
						payload.poisonPayload,
						resist.immunityResValue,
						cfg,
						nowSec);

					if (Math::MeterPops(state.poisonMeter)) {
						poisonPopped = true;
						// Reset meter on pop.
						state.poisonMeter = 0.0f;
						state.poisonLastBuildupSec = nowSec;

						StatusProcMessage msg{};
						msg.attackerFormId = attacker->GetFormID();
						msg.targetFormId = target->GetFormID();
						msg.statusId = Status_Poison;
						msg.band = Band_Immunity;
						msg.meterBeforePop = poisonBefore;
						msg.payloadAtPop = payload.poisonPayload;
						EmitStatusProc(msg);
					}
				}

				// Bleed meter update
				if (payload.bleedPayload > 0.0f) {
					bleedBefore = state.bleedMeter;
					ApplyDecayAndAccumulate(
						state.bleedMeter,
						state.bleedLastBuildupSec,
						payload.bleedPayload,
						resist.robustnessResValue,
						cfg,
						nowSec);

					if (Math::MeterPops(state.bleedMeter)) {
						bleedPopped = true;
						state.bleedMeter = 0.0f;
						state.bleedLastBuildupSec = nowSec;

						StatusProcMessage msg{};
						msg.attackerFormId = attacker->GetFormID();
						msg.targetFormId = target->GetFormID();
						msg.statusId = Status_Bleed;
						msg.band = Band_Robustness;
						msg.meterBeforePop = bleedBefore;
						msg.payloadAtPop = payload.bleedPayload;
						EmitStatusProc(msg);
					}
				}

				poisonAfter = state.poisonMeter;
				bleedAfter = state.bleedMeter;
			}

			// Debug log per hit (outside the meter lock).
			if (shouldLogHit && (consoleSelectedMatchesTarget || allowLogEvenIfSelectionMismatch)) {
				ERCFLog::LineF(
					"ERCF DBG Hit #%llu sel=%u(%s) attacker=%u target=%u hitTotal=%.3f hitPhys=%.3f payload(poison=%.3f bleed=%.3f) resist(imm=%.3f rob=%.3f) meter(before: p=%.2f b=%.2f after: p=%.2f b=%.2f) pop(poison=%u bleed=%u)",
					static_cast<unsigned long long>(hitIndex),
					static_cast<std::uint32_t>(consoleSelectedFormId),
					consoleSelectedMatchesTarget ? "match" : "mismatch",
					attacker->GetFormID(),
					target->GetFormID(),
					static_cast<float>(hitTotalDamage),
					static_cast<float>(hitPhysicalDamage),
					payload.poisonPayload,
					payload.bleedPayload,
					static_cast<float>(resistImmunity),
					static_cast<float>(resistRobustness),
					poisonBefore,
					bleedBefore,
					poisonAfter,
					bleedAfter,
					poisonPopped ? 1u : 0u,
					bleedPopped ? 1u : 0u
				);
			}

			if (playerTarget) {
				ERCFLog::LineF("ERCF: Player meters poison=%.2f bleed=%.2f", poisonAfter, bleedAfter);
				Prisma::UpdateMeters(poisonAfter, bleedAfter);
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	}
}

