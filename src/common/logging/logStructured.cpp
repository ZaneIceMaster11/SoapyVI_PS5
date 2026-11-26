#include "common/logging/logCategory.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <system_error>
#include <vector>

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Log {

namespace {

// 64 MiB per file, five files: a verbose session cannot grow without bound.
constexpr size_t MAX_FILE_BYTES = 64ull * 1024 * 1024;
constexpr size_t MAX_FILES      = 5;

std::atomic<Level> g_min_level {Level::Info};
// Never destroyed: guest threads keep logging while exit runs static destructors.
spdlog::logger* g_structured = nullptr;

} // namespace

const char* LevelName(Level level) noexcept {
	switch (level) {
		case Level::Trace: return "TRACE";
		case Level::Debug: return "DEBUG";
		case Level::Info: return "INFO";
		case Level::Warn: return "WARN";
		case Level::Error: return "ERROR";
		case Level::Critical: return "CRIT";
		case Level::Off: return "OFF";
	}
	return "?";
}

const char* CategoryName(Category category) noexcept {
	switch (category) {
		case Category::General: return "General";
		case Category::Kernel: return "Kernel";
		case Category::Loader: return "Loader";
		case Category::Gpu: return "Gpu";
		case Category::Shader: return "Shader";
		case Category::Pipeline: return "Pipeline";
		case Category::Audio: return "Audio";
		case Category::Input: return "Input";
		case Category::Hle: return "Hle";
		case Category::Fs: return "Fs";
		case Category::Net: return "Net";
		case Category::Perf: return "Perf";
		case Category::Crash: return "Crash";
		case Category::Guest: return "Guest";
		case Category::Count: break;
	}
	return "?";
}

void SetMinLevel(Level level) noexcept {
	g_min_level.store(level, std::memory_order_relaxed);
}

Level GetMinLevel() noexcept {
	return g_min_level.load(std::memory_order_relaxed);
}

bool Enabled(Level level) noexcept {
	const auto min = g_min_level.load(std::memory_order_relaxed);
	return min != Level::Off && static_cast<int>(level) >= static_cast<int>(min);
}

std::string FormatLine(Level level, Category category, std::string_view text) {
	if (!text.empty() && text.back() == '\n') {
		text.remove_suffix(1);
	}
	return fmt::sprintf("[%-5s] [%-8s] %s", LevelName(level), CategoryName(category),
	                    std::string(text));
}

void InitStructured(const std::filesystem::path& file, Level min_level, bool echo_console) {
	g_min_level.store(min_level, std::memory_order_relaxed);
	if (min_level == Level::Off) {
		return;
	}
	if (file.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
	}
	std::vector<spdlog::sink_ptr> sinks;
	// spdlog here uses wide filenames (filename_t = std::wstring on Windows), so pass native().
	sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
	    file.native(), MAX_FILE_BYTES, MAX_FILES));
	if (echo_console) {
		sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
	}
	auto* logger = new spdlog::logger("kyty.structured", sinks.begin(), sinks.end());
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
	logger->set_level(spdlog::level::info);
	// Flush eagerly on warnings and above so a crash cannot swallow the final diagnostics.
	logger->flush_on(spdlog::level::warn);
	g_structured = logger;
}

void FlushStructured() {
	if (g_structured != nullptr) {
		g_structured->flush();
	}
}

void Message(Level level, Category category, std::string_view text) {
	if (!Enabled(level)) {
		return;
	}
	const auto line = FormatLine(level, category, text);
	if (g_structured == nullptr) {
		// Before InitStructured, keep early diagnostics instead of dropping them.
		std::fprintf(stderr, "%s\n", line.c_str());
		return;
	}
	g_structured->log(spdlog::level::info, spdlog::string_view_t(line.data(), line.size()));
}

} // namespace Log
