#include "pch.h"

#include "Messaging.h"

#include "Log.h"

namespace ERCF
{
	void EmitStatusProc(const StatusProcMessage& a_msg)
	{
		if (auto* messaging = SKSE::GetMessagingInterface()) {
			// Receiver=null => broadcast to all listeners registered for sender "ERCF".
			const auto ok = messaging->Dispatch(
				kStatusProcMessageType,
				(void*)std::addressof(a_msg),
				static_cast<std::uint32_t>(sizeof(StatusProcMessage)),
				nullptr);
			if (!ok) {
				ERCFLog::Line("ERCF: EmitStatusProc dispatch failed");
			}
		}
	}
}

