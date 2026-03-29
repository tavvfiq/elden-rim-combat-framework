#pragma once

#include <cstdint>

namespace ERCF
{
	namespace Prisma
	{
		// Resolves PrismaUI API on kDataLoaded; optional immediate CreateView (config).
		void Init();

		// Hit-deferred path: publish when any ERCF status meter changed vs last published (buildup up or down, fuzz).
		// At 0 (incl. after proc pop) the overlay hides; no peak flash.
		// Dispatches SKSE Messaging (EmitPrismaHudBuildupRefresh); listener runs try_update — not on hit stack.
		void OnPlayerBuildupHudEvent(
			float poisonForHud,
			float bleedForHud,
			float rotForHud,
			float frostbiteForHud,
			float sleepForHud,
			float madnessForHud);

		// Registered in main: sender ERCF, type kPrismaHudBuildupRefreshMessageType (no payload).
		void OnPrismaHudBuildupMessaging();

		// Starts HUD poll (and deferred CreateView if configured). Prefer QueueStartPeriodicHudRefresh from lifecycle.
		void StartPeriodicHudRefresh();

		// Schedules StartPeriodicHudRefresh on the SKSE game thread — required from kPostLoadGame / messaging
		// (calling StartPeriodicHudRefresh synchronously there can hard-freeze).
		void QueueStartPeriodicHudRefresh();

		// Stops the task chain (title menu / quit to main). Safe to call multiple times.
		void StopPeriodicHudRefresh();

		// Elden Ring–style top banner when a player status procs (ERCF Status_* id from Messaging.h).
		void QueuePlayerStatusProcBanner(std::uint32_t a_statusId);
	}
}

