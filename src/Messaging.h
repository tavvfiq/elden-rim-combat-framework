#pragma once

#include <cstdint>
#include <type_traits>

namespace ERCF
{
	// Used as the SKSE Messaging "sender" name in RegisterListener(...).
	// Keep this identical to the plugin's configured name in xmake.lua.
	inline constexpr const char* kSenderName = "ERCF";

	// Stable message type id for the public proc event.
	// Other mods can listen for this by registering for sender "ERCF" and checking msg->type.
	inline constexpr std::uint32_t kStatusProcMessageType = 0x45524346;  // ASCII "ERCF"

	enum : std::uint32_t
	{
		// v1 status ids (contract aligned).
		Status_Poison = 1,
		Status_Bleed = 2,
	};

	enum : std::uint32_t
	{
		// v1 resistance bands (contract aligned).
		Band_Immunity = 1,
		Band_Robustness = 2,
	};

	// Public messaging payload POD.
	// Notes:
	// - We use FormIDs (stable identifiers) rather than runtime pointers.
	// - External mods can optionally look up active refs by FormID.
	struct StatusProcMessage
	{
		std::uint32_t attackerFormId = 0;
		std::uint32_t targetFormId = 0;
		std::uint32_t statusId = 0;
		std::uint32_t band = 0;
		float meterBeforePop = 0.0f;
		// Numeric magnitude snapshot used by the proc handler (v1).
		// For v1, the proc handler treats this as the initial damage amount to apply.
		float payloadAtPop = 0.0f;
	};

	// Compile-time check: keep it POD/ABI-stable across compilers.
	static_assert(std::is_standard_layout_v<StatusProcMessage>);

	// Emits a public ERCF.StatusProc SKSE messaging event.
	// v1: dispatches synchronously; internal listener will apply proc effects.
	void EmitStatusProc(const StatusProcMessage& a_msg);
}

