#include "common/logging/logCategory.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace {

int g_failures = 0;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CHECK(cond)                                                                                \
	do {                                                                                           \
		if (!(cond)) {                                                                             \
			std::printf("StructuredLogTests: FAIL (line %d): %s\n", __LINE__, #cond);              \
			++g_failures;                                                                          \
		}                                                                                          \
	} while (false)

void TestLevelGating() {
	using namespace Log;
	SetMinLevel(Level::Warn);
	CHECK(!Enabled(Level::Trace));
	CHECK(!Enabled(Level::Debug));
	CHECK(!Enabled(Level::Info));
	CHECK(Enabled(Level::Warn));
	CHECK(Enabled(Level::Error));
	CHECK(Enabled(Level::Critical));

	SetMinLevel(Level::Off);
	CHECK(!Enabled(Level::Trace));
	CHECK(!Enabled(Level::Critical));

	SetMinLevel(Level::Trace);
	CHECK(Enabled(Level::Trace));
	CHECK(Enabled(Level::Info));
	CHECK(GetMinLevel() == Level::Trace);
}

void TestNames() {
	using namespace Log;
	CHECK(std::string(LevelName(Level::Trace)) == "TRACE");
	CHECK(std::string(LevelName(Level::Info)) == "INFO");
	CHECK(std::string(LevelName(Level::Error)) == "ERROR");
	CHECK(std::string(CategoryName(Category::General)) == "General");
	CHECK(std::string(CategoryName(Category::Shader)) == "Shader");
	CHECK(std::string(CategoryName(Category::Crash)) == "Crash");
	// Every category must have a distinct, non-placeholder name.
	for (int i = 0; i < static_cast<int>(Category::Count); ++i) {
		CHECK(std::string(CategoryName(static_cast<Category>(i))) != "?");
	}
}

void TestFormatLine() {
	using namespace Log;
	const auto line = FormatLine(Level::Error, Category::Gpu, "boom\n");
	CHECK(line.find("[ERROR") != std::string::npos);
	CHECK(line.find("[Gpu") != std::string::npos);
	CHECK(line.find("boom") != std::string::npos);
	// The single trailing newline is trimmed.
	CHECK(line.find('\n') == std::string::npos);

	// An empty message still formats without crashing.
	const auto empty = FormatLine(Level::Info, Category::Kernel, "");
	CHECK(empty.find("[INFO") != std::string::npos);
	CHECK(empty.find("[Kernel") != std::string::npos);
}

// Exercises the real spdlog file sink end-to-end: init, log through the macros, read it back.
void TestFileOutput() {
	using namespace Log;
	namespace fs = std::filesystem;
	std::error_code ec;
	const auto      dir  = fs::temp_directory_path() / "kyty_structured_log_test";
	const auto      file = dir / "session.log";
	fs::remove_all(dir, ec);

	InitStructured(file, Level::Debug, /*echo_console=*/false);
	KLOGI(Category::Gpu, "hello %d", 42);
	KLOGD(Category::Shader, "translated %s", "vs_main");
	KLOGT(Category::Kernel, "trace below min %d", 1); // Trace < Debug: must be filtered out
	FlushStructured();

	std::ifstream in(file);
	CHECK(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string contents = ss.str();
	CHECK(contents.find("hello 42") != std::string::npos);
	CHECK(contents.find("[INFO") != std::string::npos);
	CHECK(contents.find("[Gpu") != std::string::npos);
	CHECK(contents.find("translated vs_main") != std::string::npos);
	CHECK(contents.find("trace below min") == std::string::npos);

	fs::remove_all(dir, ec);
}

} // namespace

int main() {
	TestLevelGating();
	TestNames();
	TestFormatLine();
	TestFileOutput();
	if (g_failures == 0) {
		std::printf("StructuredLogTests: all passed\n");
		return 0;
	}
	std::printf("StructuredLogTests: %d failure(s)\n", g_failures);
	return 1;
}
