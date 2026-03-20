#include "pch.h"

#include "Log.h"
#include "Messaging.h"
#include "Proc.h"
#include "HitEventHandler.h"
#include "PrismaUIMeters.h"

namespace
{
	void OnStatusProcMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		if (a_msg->type != ERCF::kStatusProcMessageType) {
			return;
		}
		if (a_msg->dataLen < sizeof(ERCF::StatusProcMessage) || !a_msg->data) {
			ERCFLog::Line("ERCF: StatusProc message invalid payload (size/data)");
			return;
		}

		auto* payload = static_cast<ERCF::StatusProcMessage*>(a_msg->data);
		ERCFLog::LineF(
			"ERCF: StatusProc received (attacker=%u target=%u status=%u band=%u meter=%.3f payload=%.3f)",
			payload->attackerFormId,
			payload->targetFormId,
			payload->statusId,
			payload->band,
			payload->meterBeforePop,
			payload->payloadAtPop
		);

		// Apply the numeric proc effects for Poison/Bleed.
		ERCF::Proc::ApplyStatusProc(*payload);
	}

	void OnSkseLifecycleMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			ERCFLog::Line("ERCF: kDataLoaded");
			ERCF::Config::Load();
			ERCF::Runtime::HitEventHandler::Register();
			ERCF::Prisma::Init();
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
	ERCFLog::Line("ERCF: plugin loaded");

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		ERCFLog::Line("ERCF: missing SKSE messaging interface");
		return false;
	}

	// Receive ERCF public proc messages emitted by this plugin (and potentially others).
	if (!messaging->RegisterListener(ERCF::kSenderName, OnStatusProcMessage)) {
		ERCFLog::Line("ERCF: failed to register StatusProc listener");
	}

	// Receive SKSE lifecycle messages to initialize our state on kDataLoaded.
	if (!messaging->RegisterListener("SKSE", OnSkseLifecycleMessage)) {
		ERCFLog::Line("ERCF: failed to register SKSE lifecycle listener");
	}

	return true;
}

