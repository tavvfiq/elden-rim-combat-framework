#pragma once

#include <cstdint>

namespace ERCF
{
	namespace Prisma
	{
		// Resolves PrismaUI API on kDataLoaded; CreateView (Souls Style Looting pattern).
		void Init();

		// Hit-deferred path: publish when any ERCF status meter changed vs last published (buildup up or down, fuzz).
		// Decay-driven updates use the same path from StatusEffectManager (Actor::Update → QueueFlushTask).
		void OnPlayerBuildupHudEvent(
			float poisonForHud,
			float bleedForHud,
			float rotForHud,
			float frostbiteForHud,
			float sleepForHud,
			float madnessForHud);

		// Registered in main: sender ERCF, type kPrismaHudBuildupRefreshMessageType (no payload).
		void OnPrismaHudBuildupMessaging();

		// Elden Ring–style top banner when a player status procs (ERCF Status_* id from Messaging.h).
		void QueuePlayerStatusProcBanner(std::uint32_t a_statusId);

		// Player Actor::Update post hook: proc-banner hold expiry → clear DOM + queue try_update.
		void TickProcBannerExpiryOnPlayerUpdate();

		// Player Actor::Update post hook: complete deferred Prisma Hide after WebView CSS fade-out.
		void TickDeferredNativeHudHide();
	}
}
