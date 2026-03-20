#include "pch.h"

#include "Proc.h"

#include "Log.h"

#include "CombatMath.h"
#include "Config.h"
#include "EspRouting.h"
#include "RE/A/Actor.h"
#include "RE/T/TESForm.h"

namespace ERCF
{
	namespace Proc
	{
		void ApplyStatusProc(const StatusProcMessage& a_msg)
		{
			if (a_msg.targetFormId == 0) {
				ERCFLog::Line("ERCF: ApplyStatusProc failed: targetFormId=0");
				return;
			}
			if (a_msg.payloadAtPop <= 0.0f) {
				// Allow 0/negative payloads so meters can emit events for debug without crashing.
				return;
			}

			// v1 lookup:
			// - If `targetFormId` is a placed actor reference FormID, TESForm lookup may still find it.
			// - If not found, we no-op (better than hard crash).
			auto* target = RE::TESForm::LookupByID<RE::Actor>(a_msg.targetFormId);
			if (!target) {
				ERCFLog::LineF("ERCF: ApplyStatusProc: target Actor not found (formId=%u)", a_msg.targetFormId);
				return;
			}
			if (target->IsDead()) {
				return;
			}

			// v1 "HP pipeline contract":
			// Proc numeric damage should, by default, travel through the same
			// Defense (flat/non-linear) -> Absorption (multiplicative) pipeline.
			//
			// Since we haven't authored per-status damage-type yet in code,
			// we map v1:
			// - Poison -> Magic
			// - Bleed  -> Standard
			Esp::DamageTypeId dmgType = Esp::DamageTypeId::Standard;
			if (a_msg.statusId == Status_Poison) {
				dmgType = Esp::DamageTypeId::Magic;
			} else if (a_msg.statusId == Status_Bleed) {
				dmgType = Esp::DamageTypeId::Standard;
			}

			const auto cfg = Config::Get();
			Esp::MitigationCoefficients mitigation{};
			Esp::ExtractMitigationFromActiveEffects(target, mitigation);

			const std::size_t idx = static_cast<std::size_t>(dmgType);
			const float finalDamage = Math::DamageAfterDefenseAndAbsorption(
				a_msg.payloadAtPop,
				mitigation.defense[idx],
				mitigation.absorptionFractions[idx],
				cfg);

			if (auto* avOwner = target->AsActorValueOwner()) {
				avOwner->ModActorValue(RE::ActorValue::kHealth, -finalDamage);
			}
		}
	}
}

