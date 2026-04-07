#include "pch.h"

#include "Log.h"
#include "Messaging.h"
#include "Config.h"
#include "ERASIntegration.h"
#include "HitEventHandler.h"
#include "PrismaUIMeters.h"
#include "OverrideDamageHook.h"
#include "StatusEffectManager.h"

namespace
{
	void OnStatusProcMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		if (a_msg->type == ERCF::kPrismaHudBuildupRefreshMessageType) {
			ERCF::Prisma::OnPrismaHudBuildupMessaging();
			return;
		}
		if (a_msg->type != ERCF::kStatusProcMessageType) {
			return;
		}
		if (a_msg->dataLen < sizeof(ERCF::StatusProcBatchMessage) || !a_msg->data) {
			LOG_WARN("ERCF: StatusProc batch invalid payload (size/data)");
			return;
		}

		auto* batch = static_cast<ERCF::StatusProcBatchMessage*>(a_msg->data);
		if (batch->count == 0 || batch->count > ERCF::kStatusProcBatchMaxEntries) {
			LOG_WARN("ERCF: StatusProc batch invalid count");
			return;
		}

		for (std::uint32_t i = 0; i < batch->count; ++i) {
			const auto& payload = batch->entries[i];
			LOG_INFO(
				"ERCF: StatusProc received [{}/{}] (attacker={} target={} status={} band={} meter={:.3f} payload={:.3f})",
				i + 1,
				batch->count,
				payload.attackerFormId,
				payload.targetFormId,
				payload.statusId,
				payload.band,
				payload.meterBeforePop,
				payload.payloadAtPop);
			// Procs are applied inside StatusEffectManager before this batch is emitted; avoid double-apply.
			// External listeners that need gameplay effects should call ERCF::StatusEffects::ApplyStatusProcMessage.
		}
	}

	void OnSkseLifecycleMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			ERCFLog::EnsureLogPath();
			LOG_INFO("ERCF: kDataLoaded");
			ERCF::Config::Load();
			ERCF::ERAS::Init();
			ERCF::Override::Install();
			ERCF::Runtime::HitEventHandler::Register();
			ERCF::StatusEffects::InstallActorDecayHook();
			ERCF::Prisma::Init();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			ERCFLog::TruncateForGameplaySession();
			LOG_INFO("ERCF: kPostLoadGame");
			break;
		case SKSE::MessagingInterface::kNewGame:
			ERCFLog::TruncateForGameplaySession();
			LOG_INFO("ERCF: kNewGame");
			// Safety: avoid auto-starting Prisma on New Game path (observed freeze-prone).
			// HUD can still be started later through safer runtime paths.
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	ERCFLog::Init();
	LOG_INFO("ERCF: plugin loaded");

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		LOG_ERROR("ERCF: missing SKSE messaging interface");
		return false;
	}

	// Receive ERCF public proc messages emitted by this plugin (and potentially others).
	if (!messaging->RegisterListener(ERCF::kSenderName, OnStatusProcMessage)) {
		LOG_ERROR("ERCF: failed to register StatusProc listener");
	}

	// Receive SKSE lifecycle messages (kDataLoaded/kPostLoadGame/kNewGame).
	// Use no-sender overload for broad compatibility across SKSE setups.
	if (!messaging->RegisterListener(OnSkseLifecycleMessage)) {
		LOG_ERROR("ERCF: failed to register SKSE lifecycle listener");
	}

	return true;
}

