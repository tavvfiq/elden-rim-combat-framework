#pragma once

#include "ERLS_API.h"

namespace ERCF::ERLS
{
	// Resolve ERLS runtime API (optional). Safe to call multiple times.
	void Init();

	[[nodiscard]] bool IsAvailable();

	// Player-only snapshot. Returns false if ERLS not installed or API call fails.
	[[nodiscard]] bool TryGetPlayerSnapshot(ERLS_API::PlayerStatsSnapshot& out);
}

