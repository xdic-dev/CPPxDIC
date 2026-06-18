/**
 * @file logging.cpp
 * @brief Implementation of the CPPxDIC logging facility (see logging.h).
 */

#include "logging.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>

#if defined(_WIN32)
#include <io.h>
#define CPPXDIC_ISATTY(fd) _isatty(fd)
#define CPPXDIC_FILENO(f) _fileno(f)
#else
#include <unistd.h>
#define CPPXDIC_ISATTY(fd) ::isatty(fd)
#define CPPXDIC_FILENO(f) ::fileno(f)
#endif

namespace cppxdic {
namespace log {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* baseName(const char* path) {
    if (!path) return "";
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

const char* colorFor(Level l) {
    switch (l) {
        case Level::Trace:
            return "\033[37m"; // grey
        case Level::Debug:
            return "\033[36m"; // cyan
        case Level::Info:
            return "\033[32m"; // green
        case Level::Warn:
            return "\033[33m"; // yellow
        case Level::Error:
            return "\033[31m"; // red
        default:
            return "";
    }
}
const char* colorReset() {
    return "\033[0m";
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms));
    return out;
}

std::string rtrim(const std::string& s) {
    std::size_t end = s.size();
    while (end > 0) {
        const char c = s[end - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            --end;
        else
            break;
    }
    return s.substr(0, end);
}

} // namespace

Level levelFromString(const std::string& s, Level fallback) {
    const std::string v = toLower(s);
    if (v == "trace") return Level::Trace;
    if (v == "debug") return Level::Debug;
    if (v == "info") return Level::Info;
    if (v == "warn" || v == "warning") return Level::Warn;
    if (v == "error" || v == "err") return Level::Error;
    if (v == "off" || v == "none" || v == "silent") return Level::Off;
    return fallback;
}

const char* levelName(Level l) {
    switch (l) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO ";
        case Level::Warn:
            return "WARN ";
        case Level::Error:
            return "ERROR";
        case Level::Off:
            return "OFF  ";
    }
    return "?????";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    color_ = CPPXDIC_ISATTY(CPPXDIC_FILENO(stderr)) != 0;
}

void Logger::ensureEnv() {
    if (!env_done_) {
        env_done_ = true;
        configureFromEnv();
    }
}

void Logger::configureFromEnv() {
    if (const char* lvl = std::getenv("CPPXDIC_LOG_LEVEL")) {
        console_level_ = levelFromString(lvl, console_level_);
    }
    if (const char* con = std::getenv("CPPXDIC_LOG_CONSOLE")) {
        const std::string v = toLower(con);
        console_enabled_ = !(v == "0" || v == "false" || v == "off" || v == "no");
    }
    if (const char* path = std::getenv("CPPXDIC_LOG_FILE")) {
        if (path[0] != '\0') file_.open(path, std::ios::out | std::ios::app);
    }
}

void Logger::setConsoleLevel(Level l) {
    std::lock_guard<std::mutex> lk(mtx_);
    console_level_ = l;
}

void Logger::setFileLevel(Level l) {
    std::lock_guard<std::mutex> lk(mtx_);
    file_level_ = l;
}

Level Logger::consoleLevel() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return console_level_;
}

Level Logger::fileLevel() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return file_level_;
}

bool Logger::setFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (file_.is_open()) file_.close();
    if (path.empty()) return true;
    file_.open(path, std::ios::out | std::ios::app);
    return file_.is_open();
}

void Logger::setConsoleEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mtx_);
    console_enabled_ = on;
}

void Logger::setVerboseFormat(bool on) {
    std::lock_guard<std::mutex> lk(mtx_);
    verbose_format_ = on;
}

bool Logger::enabled(Level l) {
    std::lock_guard<std::mutex> lk(mtx_);
    ensureEnv();
    if (l == Level::Off) return false;
    const bool to_console = console_enabled_ && l >= console_level_;
    const bool to_file = file_.is_open() && l >= file_level_;
    return to_console || to_file;
}

void Logger::write(Level l, const char* file, int line, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mtx_);
    ensureEnv();
    if (l == Level::Off) return;

    const std::string text = rtrim(msg);
    const char* fname = baseName(file);

    if (console_enabled_ && l >= console_level_) {
        std::ostream& os = (l >= Level::Warn) ? std::cerr : std::cout;
        if (color_) os << colorFor(l);
        os << "[" << levelName(l) << "]";
        if (color_) os << colorReset();
        if (verbose_format_) os << " " << timestamp() << " " << fname << ":" << line;
        os << " " << text << "\n";
    }

    if (file_.is_open() && l >= file_level_) {
        file_ << timestamp() << " [" << levelName(l) << "] " << fname << ":" << line << " " << text
              << "\n";
        file_.flush();
    }
}

void setLevel(Level l) {
    Logger::instance().setConsoleLevel(l);
}

bool setFile(const std::string& path) {
    return Logger::instance().setFile(path);
}

void configureFromOptions(const Options& opts) {
    Logger& lg = Logger::instance();

    // 1+2. Apply environment first (establishes the baseline console level/file).
    lg.configureFromEnv();

    // 3+4. Explicit level from config/CLI overrides the env baseline.
    Level level = lg.consoleLevel();
    if (opts.console_level_set) level = opts.console_level;

    // Relative adjustments: -V lowers the threshold, -q raises it.
    auto step_down = [](Level x, int n) {
        int v = static_cast<int>(x) - n;
        if (v < static_cast<int>(Level::Trace)) v = static_cast<int>(Level::Trace);
        return static_cast<Level>(v);
    };
    if (opts.verbose > 0) level = step_down(level, opts.verbose);
    if (opts.quiet) level = Level::Warn;

    // Debug mode forces at least Debug verbosity and turns on rich formatting.
    if (opts.debug && level > Level::Debug) level = Level::Debug;
    lg.setVerboseFormat(opts.debug);

    lg.setConsoleLevel(level);

    if (!opts.log_file.empty()) lg.setFile(opts.log_file);

    // Propagate to the ncorr library (it reads these on first use). setenv keeps
    // the engine's logs aligned with this process's level and file without a
    // compile-time dependency on ncorr's logger.
    const char* name = levelName(level);
    std::string lname(name);
    // Trim trailing padding spaces from the fixed-width name.
    while (!lname.empty() && lname.back() == ' ') lname.pop_back();
    std::transform(lname.begin(), lname.end(), lname.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#if !defined(_WIN32)
    setenv("NCORR_LOG_LEVEL", lname.c_str(), 1);
    if (!opts.log_file.empty()) setenv("NCORR_LOG_FILE", opts.log_file.c_str(), 1);
#endif
}

} // namespace log
} // namespace cppxdic
