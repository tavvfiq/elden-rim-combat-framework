#include "pch.h"

#include "Messaging.h"

#include "Log.h"

namespace ERCF
{
	void EmitStatusProcBatch(const StatusProcBatchMessage& a_batch)
	{
		if (a_batch.count == 0 || a_batch.count > kStatusProcBatchMaxEntries) {
			LOG_WARN("ERCF: EmitStatusProcBatch invalid count");
			return;
		}
		if (auto* messaging = SKSE::GetMessagingInterface()) {
			// Local copy: Dispatch takes non-const void*; listeners must treat payload as read-only.
			StatusProcBatchMessage copy = a_batch;
			const auto ok = messaging->Dispatch(
				kStatusProcMessageType,
				static_cast<void*>(std::addressof(copy)),
				static_cast<std::uint32_t>(sizeof(StatusProcBatchMessage)),
				nullptr);
			if (!ok) {
				LOG_WARN("ERCF: EmitStatusProcBatch dispatch failed");
			}
		}
	}

	void EmitStatusProc(const StatusProcMessage& a_msg)
	{
		StatusProcBatchMessage batch{};
		batch.count = 1;
		batch.entries[0] = a_msg;
		EmitStatusProcBatch(batch);
	}

	void EmitPrismaHudBuildupRefresh()
	{
		if (auto* messaging = SKSE::GetMessagingInterface()) {
			const auto ok = messaging->Dispatch(
				kPrismaHudBuildupRefreshMessageType,
				nullptr,
				0,
				nullptr);
			if (!ok) {
				LOG_WARN("ERCF: EmitPrismaHudBuildupRefresh dispatch failed");
			}
		}
	}
}

