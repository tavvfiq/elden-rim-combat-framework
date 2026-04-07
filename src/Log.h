#pragma once

#include "pch.h"

#include <SKSE/Logger.h>

#include <spdlog/common.h>
#include <string_view>

namespace logger = SKSE::log;

// fmt-style formatting (same as SKSE::log / spdlog); use "{}" not printf specifiers.
#define LOG_INFO(...) logger::info(__VA_ARGS__)
#define LOG_ERROR(...) logger::error(__VA_ARGS__)
#define LOG_WARN(...) logger::warn(__VA_ARGS__)
#define LOG_DEBUG(...) logger::debug(__VA_ARGS__)
#define LOG_TRACE(...) logger::trace(__VA_ARGS__)
#define LOG_CRITICAL(...) logger::critical(__VA_ARGS__)

namespace ERCFLog
{
	void Init();

	void EnsureLogPath();

	void TruncateForGameplaySession();

	[[nodiscard]] spdlog::level::level_enum ParseLogLevel(std::string_view a_sv) noexcept;

	void SetLogLevel(spdlog::level::level_enum a_level) noexcept;

	[[nodiscard]] spdlog::level::level_enum GetLogLevel() noexcept;

	[[nodiscard]] const char* LevelConfigString(spdlog::level::level_enum a_level) noexcept;
}
