#pragma once

#include "Messaging.h"

#include "RE/T/TESForm.h"

#include <cstdint>

namespace RE
{
	class Actor;
}

namespace ERCF
{
	namespace Esp
	{
		struct StatusResistanceCoefficients;
	}

	namespace StatusEffects
	{
		// Aligns with Messaging status ids.
		enum class Type : std::uint32_t
		{
			Poison = 1,
			Bleed = 2,
			Rot = 3,
			Frostbite = 4,
			Sleep = 5,
			Madness = 6,
		};

		struct Thresholds
		{
			float immunity = 0.0f;
			float robustness = 0.0f;
			float focus = 0.0f;
			float madness = 0.0f;
		};

		struct ProcRequirementSnapshot
		{
			float attackerWeaponPhysicalDamage = 0.0f;
			float targetMaxHp = 0.0f;
			float targetMaxMagicka = 0.0f;
			float targetMaxStamina = 0.0f;
		};

		struct ProcInput
		{
			std::uint32_t attackerFormId = 0;
			std::uint32_t targetFormId = 0;
			Type type = Type::Poison;
			float payload = 0.0f;
			// Caller-computed; if <= 0, accumulation still runs but threshold pop is skipped.
			float threshold = 0.0f;
			// Hit-time snapshot for proc damage (bleed/frost weapon terms, poison/rot/madness max-based).
			ProcRequirementSnapshot requirement{};
			// Matches hit path boss gating (e.g. ActorTypeBoss); used for boss damage branches.
			bool targetIsBoss = false;
		};

		struct PlayerMetersHudSnapshot
		{
			float poison = 0.0f;
			float bleed = 0.0f;
			float rot = 0.0f;
			float frostbite = 0.0f;
			float sleep = 0.0f;
			float madness = 0.0f;
		};

		// Fills weapon physical + target max stats (ERAS for player target when available).
		void FillProcRequirementSnapshot(RE::Actor* a_attacker, RE::Actor* a_target, ProcRequirementSnapshot& a_out);

		// Applies proc damage/effects (same rules as legacy Proc::ApplyStatusProc). Safe from messaging listener
		// when ERCF already applied internally (avoid double-apply).
		void ApplyStatusProcMessage(const StatusProcMessage& a_msg);

		// Frostbite timed debuff: +incoming damage multiplier while active.
		[[nodiscard]] float GetDamageTakenMultiplier(RE::FormID a_target);

		// Installs post-Actor::Update decay (once at kDataLoaded). Decay runs on the game thread, throttled per actor,
		// only for actors with active ERCF status meters. Paused while the title main menu is open.
		void InstallActorDecayHook();

		// Thread-safe: accumulates meter, may queue proc message(s) for next deferred flush.
		void ProcStatusEffect(const ProcInput& a_input);

		// Coalesce SKSE proc batch + player Prisma refresh on SKSE task (call once after a hit's ProcStatusEffect calls).
		void RequestDeferredFlush();

		// Current status meter storage (decay is applied on Actor::Update; no extra analytic step here).
		[[nodiscard]] bool TryGetMeterSnapshotForHud(std::uint32_t a_targetFormId, PlayerMetersHudSnapshot& a_out);

		[[nodiscard]] bool TryGetPlayerMetersHudSnapshot(PlayerMetersHudSnapshot& a_out);

		[[nodiscard]] bool TryGetPlayerMeterSnapshotForHud(float& poisonOut, float& bleedOut);

		// Thresholds use internal proc counts; call from hit path with resolved resist bands.
		[[nodiscard]] Thresholds ComputeThresholds(
			std::uint32_t a_targetFormId,
			RE::Actor* a_target,
			bool a_isPlayer,
			const Esp::StatusResistanceCoefficients& a_resist);
	}
}
