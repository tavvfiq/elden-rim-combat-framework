#include "pch.h"

#include "ERASIntegration.h"

#include "Log.h"

#include "RE/P/PlayerCharacter.h"

#include <atomic>

namespace ERCF::ERAS
{
	namespace
	{
		std::atomic<ERAS_API::IERAS1*> s_api{ nullptr };
		std::atomic<bool> s_triedInit{ false };
	}

	void Init()
	{
		bool expected = false;
		if (!s_triedInit.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
			return;
		}

		auto* api = ERAS_API::RequestPluginAPI(ERAS_API::InterfaceVersion::V1);
		if (!api || (!api->getPlayerStats && !api->getStatsSnapshotForActor)) {
			LOG_WARN("ERCF: ERAS not found (eras.dll missing?)");
			return;
		}

		s_api.store(api, std::memory_order_release);
		LOG_INFO("ERCF: ERAS API acquired (IERAS1)");
	}

	bool IsAvailable()
	{
		return s_api.load(std::memory_order_acquire) != nullptr;
	}

	bool TryGetActorSnapshot(RE::Actor* a_actor, ERAS_API::PlayerStatsSnapshot& out)
	{
		if (!a_actor) {
			return false;
		}
		auto* api = s_api.load(std::memory_order_acquire);
		if (!api) {
			return false;
		}
		if (api->getStatsSnapshotForActor) {
			if (api->getStatsSnapshotForActor(static_cast<void*>(a_actor), std::addressof(out))) {
				return true;
			}
		}
		if (a_actor->IsPlayerRef() && api->getPlayerStats) {
			return api->getPlayerStats(std::addressof(out));
		}
		return false;
	}

	bool TryGetPlayerSnapshot(ERAS_API::PlayerStatsSnapshot& out)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->IsPlayerRef()) {
			return false;
		}
		return TryGetActorSnapshot(pc, out);
	}
}

