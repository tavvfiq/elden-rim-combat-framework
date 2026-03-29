#pragma once

#include "ERAS_API.h"

namespace RE
{
	class Actor;
}

namespace ERCF::ERAS
{
	// Resolve ERAS runtime API (optional). Safe to call multiple times.
	void Init();

	[[nodiscard]] bool IsAvailable();

	// Snapshot for any actor when ERAS exposes it (NPCs + player). Falls back to getPlayerStats for the player
	// if per-actor snapshot is unavailable. Returns false if ERAS not installed or both paths fail.
	[[nodiscard]] bool TryGetActorSnapshot(RE::Actor* a_actor, ERAS_API::PlayerStatsSnapshot& out);

	// Player-only convenience. Returns false if ERAS not installed or API call fails.
	[[nodiscard]] bool TryGetPlayerSnapshot(ERAS_API::PlayerStatsSnapshot& out);
}

