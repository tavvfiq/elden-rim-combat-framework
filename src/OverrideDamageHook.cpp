#include "pch.h"

#include "OverrideDamageHook.h"

#include "CombatMath.h"
#include "Config.h"
#include "ERLSIntegration.h"
#include "EspRouting.h"
#include "Log.h"
#include "Proc.h"

#include <RE/A/Actor.h>
#include <RE/H/HitData.h>
#include <RE/M/MagicItem.h>

namespace ERCF::Override
{
	namespace
	{
		// Re-entrancy guard: ERCF applies HP once and must not recurse through the hook.
		thread_local bool s_inERCFApply = false;

		[[nodiscard]] float ComputeRequirementDamageForPlayerVictim(
			RE::Actor* a_victim,
			const RE::HitData& a_hitData,
			float a_rawVanillaDamage)
		{
			if (!a_victim || a_rawVanillaDamage <= 0.0f) {
				return 0.0f;
			}

			const auto& cfg = ERCF::Config::Get();

			float level = static_cast<float>(a_victim->GetLevel());
			float maxHP = 0.0f;
			float maxMP = 0.0f;

			ERLS_API::PlayerStatsSnapshot snap{};
			if (ERCF::ERLS::TryGetPlayerSnapshot(snap)) {
				level = static_cast<float>(snap.erLevel);
				maxHP = static_cast<float>(snap.derived.maxHP);
				maxMP = static_cast<float>(snap.derived.maxMP);
			} else if (auto* av = a_victim->AsActorValueOwner()) {
				// Fallback when ERLS is unavailable.
				maxHP = av->GetActorValue(RE::ActorValue::kHealth);
				maxMP = av->GetActorValue(RE::ActorValue::kMagicka);
			}

			float armorRating = 0.0f;
			if (auto* av = a_victim->AsActorValueOwner()) {
				// DamageResist is the closest runtime AV source for armor rating in this phase.
				armorRating = av->GetActorValue(RE::ActorValue::kDamageResist);
			}
			const auto buckets = ERCF::Math::Req_ComputeDefenseBucketsL1(level, maxHP, maxMP, armorRating);

			// Gather current L2 mitigation sources (existing absorption fractions) once.
			ERCF::Esp::MitigationCoefficients mit{};
			ERCF::Esp::StatusResistanceCoefficients resistUnused{};
			ERCF::Esp::ExtractFromWornArmor(a_victim, cfg.armor_rating_defense_scale, mit, resistUnused);
			ERCF::Esp::MergeMitigationFromActiveActorEffects(a_victim, mit);

			float total = 0.0f;

			// Physical component (interim): take the hook raw as the physical bucket. Layer1 uses consolidated physical defense.
			const auto subtype = ERCF::Esp::ResolveDominantPhysicalSubtype(a_hitData.weapon);
			{
				const float postL1 = ERCF::Math::Req_ApplyLayer1Clamp(a_rawVanillaDamage, buckets.physical);
				const auto idx = static_cast<std::size_t>(subtype);
				const auto& percents = (idx < ERCF::Esp::kDamageTypeCount) ? mit.absorptionFractions[idx] : std::vector<float>{};
				total += ERCF::Math::Req_ApplyLayer2Mitigation(postL1, percents);
			}

			// Elemental components from weapon enchant / hit spell.
			{
				ERCF::Esp::StatusBuildupCoefficients buildupUnused{};
				ERCF::Esp::ElementalHitComponents elem{};
				const RE::MagicItem* hitSpell = a_hitData.attackDataSpell;
				const RE::TESObjectWEAP* weaponFallback = a_hitData.weapon;
				ERCF::Esp::ExtractHitSourceFromWeaponAndSpell(nullptr, hitSpell, buildupUnused, elem, weaponFallback, nullptr);

				for (std::size_t i = static_cast<std::size_t>(ERCF::Esp::DamageTypeId::Magic);
					 i < ERCF::Esp::kDamageTypeCount;
					 ++i) {
					const float rawElem = elem.attack[i] * cfg.elemental_enchant_damage_scale;
					if (!(rawElem > 0.0f)) {
						continue;
					}
					const float postL1 = ERCF::Math::Req_ApplyLayer1Clamp(rawElem, buckets.elemental);
					total += ERCF::Math::Req_ApplyLayer2Mitigation(postL1, mit.absorptionFractions[i]);
				}
			}

			const float frostMult = ERCF::Proc::GetDamageTakenMultiplier(a_victim->GetFormID());
			return (std::max)(0.0f, total * frostMult);
		}

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

				ERCFLog::Line("ERCF: OverrideDamageHook installed (pre-commit melee hit)");
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
				// Phase 0 scope: player-only override for safety.
				if (!a_victim->IsPlayerRef()) {
					return _ProcessHit(a_victim, a_hitData);
				}

				// Phase 0 proof:
				// - suppress vanilla HP damage for this hit
				// - apply exactly one ERCF HP delta afterwards
				const float vanilla = a_hitData.totalDamage;
				a_hitData.totalDamage = 0.0f;
				_ProcessHit(a_victim, a_hitData);

				const float ercfDamage = ComputeRequirementDamageForPlayerVictim(a_victim, a_hitData, vanilla);
				if (ercfDamage <= 0.0f) {
					return;
				}
				if (auto* avOwner = a_victim->AsActorValueOwner()) {
					s_inERCFApply = true;
					avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -ercfDamage);
					s_inERCFApply = false;
				}

				if (cfg.override_debug_log) {
					ERCFLog::LineF(
						"ERCF[OverrideProof]: victim=%u aggressor=%u vanilla=%.3f suppressed -> ercf=%.3f",
						a_victim->GetFormID(),
						aggressor->GetFormID(),
						vanilla,
						ercfDamage);
				}
			}

			static inline REL::Relocation<decltype(ProcessHit)> _ProcessHit;
		};
	}

	void Install()
	{
		Hook_PreCommitMeleeHit::Install();
	}
}

