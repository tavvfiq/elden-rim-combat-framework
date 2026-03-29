#include "pch.h"
#include "Log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <vector>

namespace ERCFLog
{
	namespace
	{
		std::mutex s_lock;
		std::filesystem::path s_logPath;

		void WriteLine(const char* msg)
		{
			std::lock_guard<std::mutex> lock{s_lock};
			if (s_logPath.empty()) {
				if (auto logDir = SKSE::log::log_directory()) {
					s_logPath = *logDir / "ERCF.log";
				}
			}
			if (s_logPath.empty()) {
				return;
			}

			std::ofstream f{s_logPath, std::ios::app};
			if (!f) {
				return;
			}

			auto now = std::chrono::system_clock::now();
			std::time_t t = std::chrono::system_clock::to_time_t(now);
#ifdef _WIN32
			std::tm tm{};
			localtime_s(&tm, &t);
#else
			std::tm tm{};
			localtime_r(&t, &tm);
#endif
			char buf[32]{};
			std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
			f << buf << " " << msg << "\n";
		}
	}

	void Init()
	{
		if (auto logDir = SKSE::log::log_directory()) {
			s_logPath = *logDir / "ERCF.log";
			// Fresh log each Skyrim launch (plugin load).
			{
				std::ofstream f{s_logPath, std::ios::trunc};
			}
		}
		Line("ERCF: log init");
	}

	void EnsureLogPath()
	{
		std::lock_guard<std::mutex> lock{s_lock};
		if (!s_logPath.empty()) {
			return;
		}
		if (auto logDir = SKSE::log::log_directory()) {
			s_logPath = *logDir / "ERCF.log";
		}
	}

	void TruncateForGameplaySession()
	{
		std::lock_guard<std::mutex> lock{s_lock};
		if (s_logPath.empty()) {
			if (auto logDir = SKSE::log::log_directory()) {
				s_logPath = *logDir / "ERCF.log";
			}
		}
		if (s_logPath.empty()) {
			return;
		}
		std::ofstream f{s_logPath, std::ios::trunc};
	}

	void Line(const char* msg)
	{
		WriteLine(msg);
	}

	void LineF(const char* fmt, ...)
	{
		std::va_list args;
		va_start(args, fmt);
		std::vector<char> buf(512);
		int n = std::vsnprintf(buf.data(), buf.size(), fmt, args);
		va_end(args);

		if (n < 0) {
			return;
		}

		if (static_cast<size_t>(n) >= buf.size()) {
			buf.resize(static_cast<size_t>(n) + 1);
			va_start(args, fmt);
			std::vsnprintf(buf.data(), buf.size(), fmt, args);
			va_end(args);
		}

		WriteLine(buf.data());
	}
}

