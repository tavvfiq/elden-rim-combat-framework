#include "pch.h"

#include "Log.h"

#include <cctype>
#include <algorithm>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace ERCFLog
{
	void Init()
	{
		auto logsFolder = SKSE::log::log_directory();
		if (!logsFolder) {
			SKSE::stl::report_and_fail("Unable to resolve SKSE logs directory.");
		}

		*logsFolder /= "ERCF.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logsFolder->string(), true);
		auto defaultLogger = std::make_shared<spdlog::logger>("global log", std::move(sink));

		spdlog::set_default_logger(std::move(defaultLogger));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	}

	void EnsureLogPath()
	{
	}

	void TruncateForGameplaySession()
	{
	}

	void SetLogLevel(spdlog::level::level_enum a_level) noexcept
	{
		if (const auto& lg = spdlog::default_logger()) {
			lg->set_level(a_level);
		}
	}

	spdlog::level::level_enum GetLogLevel() noexcept
	{
		if (const auto& lg = spdlog::default_logger()) {
			return lg->level();
		}
		return spdlog::level::info;
	}

	const char* LevelConfigString(spdlog::level::level_enum a_level) noexcept
	{
		switch (a_level) {
		case spdlog::level::trace:
			return "trace";
		case spdlog::level::debug:
			return "debug";
		case spdlog::level::info:
			return "info";
		case spdlog::level::warn:
			return "warn";
		case spdlog::level::err:
			return "error";
		case spdlog::level::critical:
			return "critical";
		case spdlog::level::off:
			return "off";
		default:
			return "info";
		}
	}

	spdlog::level::level_enum ParseLogLevel(std::string_view a_sv) noexcept
	{
		while (!a_sv.empty() && std::isspace(static_cast<unsigned char>(a_sv.front()))) {
			a_sv.remove_prefix(1);
		}
		while (!a_sv.empty() && std::isspace(static_cast<unsigned char>(a_sv.back()))) {
			a_sv.remove_suffix(1);
		}
		char low[16]{};
		const auto n = (std::min)(a_sv.size(), sizeof(low) - 1);
		for (std::size_t i = 0; i < n; ++i) {
			low[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(a_sv[i])));
		}
		const std::string_view s(low, n);
		if (s == "off") {
			return spdlog::level::off;
		}
		if (s == "critical") {
			return spdlog::level::critical;
		}
		if (s == "error" || s == "err") {
			return spdlog::level::err;
		}
		if (s == "warn" || s == "warning") {
			return spdlog::level::warn;
		}
		if (s == "info") {
			return spdlog::level::info;
		}
		if (s == "debug") {
			return spdlog::level::debug;
		}
		if (s == "trace") {
			return spdlog::level::trace;
		}
		return spdlog::level::info;
	}
}
