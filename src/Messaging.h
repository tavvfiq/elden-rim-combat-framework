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

	// Internal: refresh Prisma HUD after player buildup snapshot changed (no payload).
	inline constexpr std::uint32_t kPrismaHudBuildupRefreshMessageType = 0x45524347;  // ERCF + 1

	enum : std::uint32_t
	{
		// Status ids (contract aligned; extended for requirement model).
		Status_Poison = 1,
		Status_Bleed = 2,
		Status_Rot = 3,
		Status_Frostbite = 4,
		Status_Sleep = 5,
		Status_Madness = 6,
	};

	enum : std::uint32_t
	{
		// Resistance bands (contract aligned; extended for requirement model).
		Band_Immunity = 1,
		Band_Robustness = 2,
		Band_Focus = 3,
		Band_Madness = 4,
	};

	// Bits for StatusProcMessage::procContextFlags (requirement snapshot from hit path).
	enum : std::uint32_t
	{
		kProcCtxTargetIsBoss = 1u << 0,
		// When set, attackerWeaponPhysicalDamage + target max stats were filled by the caller at hit time.
		kProcCtxRequirementSnapshotValid = 1u << 1,
	};

	// Public messaging payload POD.
	// Notes:
	// - We use FormIDs (stable identifiers) rather than runtime pointers.
	// - External mods can optionally look up active refs by FormID.
	// - sizeof/layout may change across ERCF versions; external listeners should size-check or pin versions.
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
		float attackerWeaponPhysicalDamage = 0.0f;
		float targetMaxHp = 0.0f;
		float targetMaxMagicka = 0.0f;
		float targetMaxStamina = 0.0f;
		std::uint32_t procContextFlags = 0;
	};

	// Compile-time check: keep it POD/ABI-stable across compilers.
	static_assert(std::is_standard_layout_v<StatusProcMessage>);

	// One SKSE dispatch can carry up to this many procs (one per status family on a single hit).
	inline constexpr std::uint32_t kStatusProcBatchMaxEntries = 6;

	// Batched proc payload: `count` valid entries in `entries` (0..kStatusProcBatchMaxEntries).
	// Dispatch uses sizeof(StatusProcBatchMessage); unused tail slots are ignored.
	struct StatusProcBatchMessage
	{
		std::uint32_t count = 0;
		std::uint32_t reserved0 = 0;
		StatusProcMessage entries[kStatusProcBatchMaxEntries]{};
	};

	static_assert(std::is_standard_layout_v<StatusProcBatchMessage>);

	// Emits one proc as a batch of size 1 (convenience for external / single-proc callers).
	void EmitStatusProc(const StatusProcMessage& a_msg);

	// Emits all procs for this hit in a single SKSE Messaging dispatch (count must be 1..max).
	void EmitStatusProcBatch(const StatusProcBatchMessage& a_batch);

	// SKSE Messaging broadcast (sender ERCF): Prisma HUD try_update on dispatch thread (no payload).
	void EmitPrismaHudBuildupRefresh();
}

