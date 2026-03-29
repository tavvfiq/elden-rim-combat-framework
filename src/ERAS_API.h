#pragma once

#include <cstdint>
#include <Windows.h>

// Optional dependency: Elden Rim Leveling System (ERAS).
// This header is vendored from the ERAS project to consume its runtime C++ plugin API via GetModuleHandle/GetProcAddress.
namespace ERAS_API
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1 = 1
	};

	struct AttributeSet
	{
		std::int32_t vig{};
		std::int32_t mnd{};
		std::int32_t end{};
		std::int32_t str{};
		std::int32_t dex{};
		std::int32_t intl{};
		std::int32_t fth{};
		std::int32_t arc{};
	};

	struct DerivedStats
	{
		std::int32_t maxHP{};
		std::int32_t maxMP{};
		std::int32_t maxSP{};
		std::int32_t carryWeight{};
	};

	struct DefenseSheet
	{
		float physical{};
		float magic{};
		float fire{};
		float lightning{};
		float frost{};
		float poison{};
	};

	struct ThresholdSheet
	{
		float immunity{};
		float robustness{};
		float focus{};
		float vitality{};
		float madness{};
	};

	struct EquipLoadSheet
	{
		float max{};
		float light{};
		float medium{};
		float heavy{};
	};

	struct PlayerStatsSnapshot
	{
		std::int32_t erLevel{ 1 };
		AttributeSet attrs{};
		DerivedStats derived{};
		DefenseSheet defense{};
		ThresholdSheet thresholds{};
		EquipLoadSheet equipLoad{};
	};

	using GetPlayerStatsFn = bool (*)(PlayerStatsSnapshot* outSnapshot);
	using GetERLevelFn = std::int32_t (*)();
	using GetAttributesFn = bool (*)(AttributeSet* outAttrs);
	// actorPtr is a live `RE::Actor*` from the game; null is invalid.
	using GetStatsSnapshotForActorFn = bool (*)(void* actorPtr, PlayerStatsSnapshot* outSnapshot);

	struct IERAS1
	{
		GetPlayerStatsFn getPlayerStats;
		GetERLevelFn getERLevel;
		GetAttributesFn getAttributes;
		GetStatsSnapshotForActorFn getStatsSnapshotForActor;
	};

	using RequestPluginAPIFn = void* (*)(InterfaceVersion interfaceVersion);

	constexpr auto kPluginDLL = L"eras.dll";

	[[nodiscard]] inline IERAS1* RequestPluginAPI(const InterfaceVersion interfaceVersion = InterfaceVersion::V1)
	{
		const auto pluginHandle = GetModuleHandleW(kPluginDLL);
		if (!pluginHandle) {
			return nullptr;
		}

		const auto requestAPIFunction =
			reinterpret_cast<RequestPluginAPIFn>(GetProcAddress(pluginHandle, "RequestPluginAPI"));
		if (!requestAPIFunction) {
			return nullptr;
		}

		return static_cast<IERAS1*>(requestAPIFunction(interfaceVersion));
	}
}

