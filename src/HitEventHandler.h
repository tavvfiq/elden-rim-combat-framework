#pragma once

#include "RE/T/TESHitEvent.h"

#include "StatusEffectManager.h"

namespace ERCF
{
	namespace Runtime
	{
		// Evaluated ERCF status meters for the player (decay on Actor::Update). Used when pushing Prisma HUD from gameplay.
		[[nodiscard]] bool TryGetPlayerMetersHudSnapshot(StatusEffects::PlayerMetersHudSnapshot& a_out);

		[[nodiscard]] bool TryGetPlayerMeterSnapshotForHud(float& poisonOut, float& bleedOut);

		class HitEventHandler : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			static HitEventHandler* GetSingleton();

			static void Register();

			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event,
				RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

		private:
			HitEventHandler() = default;
		};
	}
}

