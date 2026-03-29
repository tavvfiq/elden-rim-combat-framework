#include "pch.h"

#include "ERLSIntegration.h"

#include "Log.h"

#include <atomic>

namespace ERCF::ERLS
{
	namespace
	{
		std::atomic<ERLS_API::IERLS1*> s_api{ nullptr };
		std::atomic<bool> s_triedInit{ false };
	}

	void Init()
	{
		bool expected = false;
		if (!s_triedInit.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
			return;
		}

		auto* api = ERLS_API::RequestPluginAPI(ERLS_API::InterfaceVersion::V1);
		if (!api || !api->getPlayerStats) {
			ERCFLog::Line("ERCF: ERLS not found (eldenrimlevelingsystem.dll missing?)");
			return;
		}

		s_api.store(api, std::memory_order_release);
		ERCFLog::Line("ERCF: ERLS API acquired (IERLS1)");
	}

	bool IsAvailable()
	{
		return s_api.load(std::memory_order_acquire) != nullptr;
	}

	bool TryGetPlayerSnapshot(ERLS_API::PlayerStatsSnapshot& out)
	{
		auto* api = s_api.load(std::memory_order_acquire);
		if (!api || !api->getPlayerStats) {
			return false;
		}
		return api->getPlayerStats(std::addressof(out));
	}
}

