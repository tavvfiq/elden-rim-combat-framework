#pragma once

#include <mutex>
#include <unordered_map>

#include "RE/T/TESHitEvent.h"

namespace ERCF
{
	namespace Runtime
	{
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

