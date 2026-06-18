/**
 * @file logging.h
 * @brief Lightweight, dependency-free logging facility for CPPxDIC.
 *
 * Replaces the project's ad-hoc std::cout/std::cerr/printf usage with a single
 * leveled logger. Design goals:
 *   - Five severity levels (Trace, Debug, Info, Warn, Error) plus Off.
 *   - Independent console and log-file thresholds, so a run can stay quiet on
 *     screen while a log file captures full Debug detail.
 *   - Thread-safe emission (internal mutex), safe under OpenMP.
 *   - Stream-style macros (LOG_INFO << ...) that short-circuit message
 *     construction when the level is disabled (cheap in hot loops).
 *
 * Verbosity vs. debug:
 *   - "Verbosity" is the console threshold: ERROR (quietest) -> TRACE (loudest).
 *     -q/--quiet raises it; -V/--verbose lowers it; --log-level sets it exactly.
 *   - "Debug" (--debug or config debug_mode=true) lowers the console threshold
 *     to Debug and turns on verbose formatting (timestamp + source location).
 *
 * Configuration precedence (low -> high), applied by configureFromOptions():
 *   1. Compiled default (Info to console, Debug to file).
 *   2. Environment: CPPXDIC_LOG_LEVEL, CPPXDIC_LOG_FILE, CPPXDIC_LOG_CONSOLE.
 *   3. Config file values (log_level, log_file, debug_mode).
 *   4. Command-line flags (-V/-q/--log-level/--log-file/--debug).
 */

#ifndef CPPXDIC_LOGGING_H
#define CPPXDIC_LOGGING_H

#include <fstream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>

namespace cppxdic {
namespace log {

/// Severity levels, ordered from most to least verbose.
enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

/// Parse a level name (case-insensitive): trace, debug, info, warn|warning,
/// error, off. Returns @p fallback if @p s is empty or unrecognized.
Level levelFromString(const std::string& s, Level fallback = Level::Info);

/// Short, fixed-width display name for a level (e.g. "INFO ").
const char* levelName(Level l);

/**
 * @brief Process-wide singleton logger. All members are thread-safe.
 */
class Logger {
public:
    static Logger& instance();

    void setConsoleLevel(Level l);
    void setFileLevel(Level l);
    Level consoleLevel() const;
    Level fileLevel() const;

    /// Open (or replace) the file sink. An empty path closes the file sink.
    /// @return false if @p path could not be opened (file sink left closed).
    bool setFile(const std::string& path);

    /// Enable or disable console output entirely.
    void setConsoleEnabled(bool on);

    /// Include timestamp + source location in console output too (the file sink
    /// always includes them). Off by default to keep the console readable.
    void setVerboseFormat(bool on);

    /// Apply CPPXDIC_LOG_LEVEL / CPPXDIC_LOG_FILE / CPPXDIC_LOG_CONSOLE. Runs
    /// once automatically on first use; calling again re-applies them.
    void configureFromEnv();

    /// True if a message at @p l would reach any sink. Use to guard expensive
    /// message construction in hot loops.
    bool enabled(Level l);

    /// Emit a fully-formed message (trailing newlines trimmed) at @p l.
    void write(Level l, const char* file, int line, const std::string& msg);

private:
    Logger();
    void ensureEnv();

    mutable std::mutex mtx_;
    Level console_level_ = Level::Info;
    Level file_level_ = Level::Debug;
    bool console_enabled_ = true;
    bool verbose_format_ = false;
    bool color_ = false;
    bool env_done_ = false;
    std::ofstream file_;
};

/// Aggregated options resolved from env/config/CLI, applied in one call.
struct Options {
    Level console_level = Level::Info;
    bool console_level_set = false; ///< true if an explicit level was chosen
    std::string log_file;           ///< empty = no file sink
    bool debug = false;             ///< debug mode (Debug level + verbose format)
    bool quiet = false;             ///< force console threshold up to Warn
    int verbose = 0;                ///< -V count: lowers threshold per step
};

/// Apply env vars, then the supplied options (CLI/config), to the singleton.
/// Also propagates equivalent settings to the ncorr library via NCORR_LOG_*
/// environment variables so the engine's logs share the same level and file.
void configureFromOptions(const Options& opts);

/// Convenience accessors.
void setLevel(Level l);
bool setFile(const std::string& path);
inline bool enabled(Level l) {
    return Logger::instance().enabled(l);
}

/**
 * @brief RAII stream builder; flushes accumulated text to the logger when it is
 *        destroyed at the end of the full expression.
 */
class Stream {
public:
    Stream(Level level, const char* file, int line) : level_(level), file_(file), line_(line) {}
    ~Stream() { Logger::instance().write(level_, file_, line_, oss_.str()); }

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    template <typename T>
    Stream& operator<<(const T& v) {
        oss_ << v;
        return *this;
    }
    /// Support stream manipulators such as std::endl / std::setprecision.
    Stream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        oss_ << manip;
        return *this;
    }

private:
    Level level_;
    const char* file_;
    int line_;
    std::ostringstream oss_;
};

/// Helper that turns the conditional logging expression into a void statement
/// (glog idiom). @c operator& has lower precedence than @c << but higher than
/// @c ?:, so the macros are safe inside an unbraced if/else.
class Voidify {
public:
    Voidify() = default;
    void operator&(Stream&) {}
};

} // namespace log
} // namespace cppxdic

// Stream-style logging macros. When the level is disabled, the right-hand side
// (message construction) is never evaluated.
#define LOG_AT(lvl)               \
    !::cppxdic::log::enabled(lvl) \
        ? (void)0                 \
        : ::cppxdic::log::Voidify() & ::cppxdic::log::Stream((lvl), __FILE__, __LINE__)

#define LOG_TRACE LOG_AT(::cppxdic::log::Level::Trace)
#define LOG_DEBUG LOG_AT(::cppxdic::log::Level::Debug)
#define LOG_INFO LOG_AT(::cppxdic::log::Level::Info)
#define LOG_WARN LOG_AT(::cppxdic::log::Level::Warn)
#define LOG_ERROR LOG_AT(::cppxdic::log::Level::Error)

#endif // CPPXDIC_LOGGING_H
