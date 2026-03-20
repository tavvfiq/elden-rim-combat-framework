// Minimal file logger for ERCF.
#pragma once

#include <cstdarg>

namespace ERCFLog
{
	// Call once during SKSEPlugin_Load.
	void Init();

	void Line(const char* msg);

	void LineF(const char* fmt, ...);
}

