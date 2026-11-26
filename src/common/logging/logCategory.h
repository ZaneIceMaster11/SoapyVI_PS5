#ifndef KYTY_COMMON_LOGGING_LOG_CATEGORY_H_
#define KYTY_COMMON_LOGGING_LOG_CATEGORY_H_

#include <fmt/printf.h>

#include <filesystem>
#include <string>
#include <string_view>

// Structured logging channel: timestamped, leveled, categorized lines written to a rotating
// session file (and optionally echoed to the console). This is independent of the guest-printf
// stream in log.h; the crash reporter, perf sink, and shader ledger all write through here.
namespace Log {

enum class Level { Trace, Debug, Info, Warn, Error, Critical, Off };

enum class Category {
	General,
	Kernel,
	Loader,
	Gpu,
	Shader,
	Pipeline,
	Audio,
	Input,
	Hle,
	Fs,
	Net,
	Perf,
	Crash,
	Guest,
	Count
};

const char* LevelName(Level level) noexcept;
const char* CategoryName(Category category) noexcept;

// Opens the rotating session file. A level of Off disables the channel. Safe to call once, after
// the config is loaded; messages logged before this go to stderr instead of being dropped.
void InitStructured(const std::filesystem::path& file, Level min_level, bool echo_console);
void FlushStructured();

void               SetMinLevel(Level level) noexcept;
Level              GetMinLevel() noexcept;
[[nodiscard]] bool Enabled(Level level) noexcept;

// "[LEVEL] [Category] text"; the sink prepends the timestamp. A single trailing newline is
// trimmed so messages migrated from LOGF("...\n") do not double-space.
std::string FormatLine(Level level, Category category, std::string_view text);

void Message(Level level, Category category, std::string_view text);

// Out of line, so callers on small guest stacks (fibers) do not reserve the formatting frame.
template <typename... Args>
[[gnu::noinline]] void MessageF(Level level, Category category, const char* format,
                                const Args&... args) {
	if (!Enabled(level)) {
		return;
	}
	Message(level, category, fmt::sprintf(format, args...));
}

} // namespace Log

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define KLOG(level, category, ...)                                                                 \
	do {                                                                                           \
		if (::Log::Enabled(level)) {                                                               \
			::Log::MessageF((level), (category), __VA_ARGS__);                                     \
		}                                                                                          \
	} while (false)
#define KLOGT(category, ...) KLOG(::Log::Level::Trace, (category), __VA_ARGS__)
#define KLOGD(category, ...) KLOG(::Log::Level::Debug, (category), __VA_ARGS__)
#define KLOGI(category, ...) KLOG(::Log::Level::Info, (category), __VA_ARGS__)
#define KLOGW(category, ...) KLOG(::Log::Level::Warn, (category), __VA_ARGS__)
#define KLOGE(category, ...) KLOG(::Log::Level::Error, (category), __VA_ARGS__)
#define KLOGC(category, ...) KLOG(::Log::Level::Critical, (category), __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif /* KYTY_COMMON_LOGGING_LOG_CATEGORY_H_ */
