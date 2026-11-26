#include "common/logging/log.h"

#include "common/emulatorConfig.h"
#include "common/logging/logCategory.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace Log {

static bool g_initialized = false;
static bool g_console     = false;
// Null when silent. Only Initialize sets it, before other threads start. Never destroyed, since
// threads keep logging while exit runs static destructors.
static spdlog::logger* g_logger = nullptr;

static void WriteStdout(std::string_view text, fmt::text_style style) {
	if (style == fmt::text_style {}) {
		std::fwrite(text.data(), 1, text.size(), stdout);
	} else {
		fmt::print(stdout, style, "{}", text);
	}
	std::fflush(stdout);
}

static Level ToStructuredLevel(Config::LogLevel level) {
	switch (level) {
		case Config::LogLevel::Trace: return Level::Trace;
		case Config::LogLevel::Debug: return Level::Debug;
		case Config::LogLevel::Info: return Level::Info;
		case Config::LogLevel::Warn: return Level::Warn;
		case Config::LogLevel::Error: return Level::Error;
		case Config::LogLevel::Off: return Level::Off;
	}
	return Level::Info;
}

static std::filesystem::path DefaultSessionLogPath() {
	const auto now  = std::chrono::system_clock::now();
	const auto time = std::chrono::system_clock::to_time_t(now);
	std::tm    tm {};
#if defined(_WIN32)
	localtime_s(&tm, &time);
#else
	localtime_r(&time, &tm);
#endif
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
	return std::filesystem::path("_Logs") / ("kyty_" + std::string(buffer) + ".log");
}

void Initialize() {
	g_initialized        = true;
	const auto direction = Config::GetPrintfDirection();
	const auto path      = Config::GetPrintfOutputFile();
	g_console            = direction == Config::LogDirection::Console;
	delete g_logger;
	g_logger = nullptr;

	spdlog::sink_ptr sink;
	if (g_console) {
		sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
	} else if (direction == Config::LogDirection::File && !path.empty()) {
		if (path.has_parent_path()) {
			std::filesystem::create_directories(path.parent_path());
		}
		const bool truncate = std::getenv(RelaunchedVariable) == nullptr;
		sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.native(), truncate);
	}
	if (sink != nullptr) {
		sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(
		    "%v", spdlog::pattern_time_type::local, ""));
		g_logger = new spdlog::logger("kyty", std::move(sink));
	}

	auto log_file = Config::GetLogFile();
	if (log_file.empty()) {
		log_file = DefaultSessionLogPath();
	}
	InitStructured(log_file, ToStructuredLevel(Config::GetLogLevel()), Config::LogConsoleEnabled());
}

void Flush() {
	if (g_logger != nullptr) {
		g_logger->flush();
	}
	FlushStructured();
}

bool IsSilent() {
	// Before Initialize the text goes to stdout.
	return g_initialized && g_logger == nullptr;
}

void Write(fmt::text_style style, std::string_view text) {
	if (!g_initialized) {
		WriteStdout(text, style);
		return;
	}
	if (g_logger == nullptr) {
		return;
	}
	if (g_console && style != fmt::text_style {}) {
		fmt::memory_buffer styled;
		fmt::format_to(std::back_inserter(styled), style, "{}", text);
		g_logger->log(spdlog::level::info, spdlog::string_view_t(styled.data(), styled.size()));
	} else {
		g_logger->log(spdlog::level::info, spdlog::string_view_t(text.data(), text.size()));
	}
}

void WriteToConsoleAndLog(std::string_view text) {
	if (g_initialized && !g_console) {
		WriteStdout(text, {});
	}
	Write({}, text);
	Flush();
}

} // namespace Log
