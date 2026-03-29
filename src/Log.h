// Minimal file logger for ERCF.
#pragma once

#include <cstdarg>

namespace ERCFLog
{
	// Call once during SKSEPlugin_Load.
	void Init();

	// If log_directory() was unavailable at plugin load, retry (e.g. from kDataLoaded).
	void EnsureLogPath();

	// Truncate ERCF.log when the player starts or loads a game session.
	void TruncateForGameplaySession();

	void Line(const char* msg);

	void LineF(const char* fmt, ...);
}

