// =============================================================================
// profiling.h  —  lightweight, env-gated time + memory (RSS) profiler.
//
// SCRATCH INSTRUMENTATION for performance profiling of the DIC pipeline
// (preprocessing / Step D / Step E / Step F). Header-only, no build-system
// changes required beyond #include. Zero overhead unless the environment
// variable XDIC_PROFILE is set.
//
// Usage:
//   #include "profiling.h"
//   { XPROF_SCOPE("D.import_video"); importVideoFrames(...); }
//   xprof::Profiler::I().dump();   // called once at the end of main()
//
// Output (when XDIC_PROFILE is set):
//   - a human-readable summary table to stderr
//   - <out>/xprof_phases.csv   aggregated per-label time + peak RSS
//   - <out>/xprof_events.csv   chronological enter/exit markers
//   - <out>/xprof_samples.csv  RSS-vs-time timeline (background sampler)
//   where <out> = $XDIC_PROFILE_OUT (default: ./xprof_out)
// =============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

namespace xprof {

// Current resident set size (bytes), portable across macOS/Linux.
inline size_t current_rss_bytes() {
#ifdef __APPLE__
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &count) == KERN_SUCCESS) {
        return static_cast<size_t>(info.resident_size);
    }
    return 0;
#else
    long rss_pages = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        long total = 0;
        if (std::fscanf(f, "%ld %ld", &total, &rss_pages) != 2) rss_pages = 0;
        std::fclose(f);
    }
    return static_cast<size_t>(rss_pages) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}

struct Phase {
    double total_ms = 0.0;
    long count = 0;
    size_t rss_peak = 0; // max RSS observed across this label's windows
};

struct Event {
    double t_ms;       // elapsed since profiler start
    std::string label; // "<name>@enter" / "<name>@exit"
    size_t rss;
};

struct Sample {
    double t_ms;
    size_t rss;
};

class Profiler {
public:
    using clock = std::chrono::steady_clock;

    static Profiler& I() {
        static Profiler inst;
        return inst;
    }

    bool enabled() const { return enabled_; }

    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(clock::now() - t0_).count();
    }

    void enter(const std::string& label) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(m_);
        events_.push_back({elapsed_ms(), label + "@enter", current_rss_bytes()});
    }

    // Called on scope exit with the measured duration.
    void record(const std::string& label, double ms) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(m_);
        size_t rss = current_rss_bytes();
        auto& ph = phases_[label];
        ph.total_ms += ms;
        ph.count++;
        if (rss > ph.rss_peak) ph.rss_peak = rss;
        events_.push_back({elapsed_ms(), label + "@exit", rss});
    }

    void dump() {
        if (!enabled_ || dumped_.exchange(true)) return;
        stop_.store(true);
        if (sampler_.joinable()) sampler_.join();

        // Compute per-phase peak RSS from the sampled timeline using enter/exit
        // windows (more accurate than the single read at scope exit).
        std::lock_guard<std::mutex> lk(m_);
        for (size_t i = 0; i < events_.size(); ++i) {
            const std::string& e = events_[i].label;
            auto at = e.rfind("@enter");
            if (at == std::string::npos) continue;
            std::string base = e.substr(0, at);
            // find matching @exit after i
            for (size_t j = i + 1; j < events_.size(); ++j) {
                if (events_[j].label == base + "@exit") {
                    size_t pk = 0;
                    for (const auto& s : samples_)
                        if (s.t_ms >= events_[i].t_ms && s.t_ms <= events_[j].t_ms)
                            if (s.rss > pk) pk = s.rss;
                    if (pk > phases_[base].rss_peak) phases_[base].rss_peak = pk;
                    break;
                }
            }
        }

        const std::string out = out_dir();
        write_phases(out + "/xprof_phases.csv");
        write_events(out + "/xprof_events.csv");
        write_samples(out + "/xprof_samples.csv");

        // Overall peak across the whole run.
        size_t overall_peak = 0;
        for (const auto& s : samples_)
            if (s.rss > overall_peak) overall_peak = s.rss;

        std::fprintf(stderr,
                     "\n================= XPROF: time + memory profile =================\n");
        std::fprintf(stderr, "%-24s %8s %10s %12s\n", "phase", "count", "time(s)", "peakRSS(MB)");
        std::fprintf(stderr, "---------------------------------------------------------------\n");
        for (const auto& kv : phases_) {
            std::fprintf(stderr, "%-24s %8ld %10.3f %12.1f\n", kv.first.c_str(), kv.second.count,
                         kv.second.total_ms / 1000.0, kv.second.rss_peak / (1024.0 * 1024.0));
        }
        std::fprintf(stderr, "---------------------------------------------------------------\n");
        std::fprintf(stderr, "%-24s %8s %10.3f %12.1f\n", "OVERALL", "", elapsed_ms() / 1000.0,
                     overall_peak / (1024.0 * 1024.0));
        std::fprintf(stderr, "CSV written to: %s/xprof_{phases,events,samples}.csv\n", out.c_str());
        std::fprintf(stderr, "===============================================================\n\n");
    }

private:
    Profiler() {
        enabled_ = std::getenv("XDIC_PROFILE") != nullptr;
        t0_ = clock::now();
        if (enabled_) {
            sampler_ = std::thread([this] {
                while (!stop_.load()) {
                    {
                        std::lock_guard<std::mutex> lk(m_);
                        samples_.push_back({elapsed_ms(), current_rss_bytes()});
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
            install_crash_handlers();
        }
    }

    // Best-effort dump on a fatal signal so a crash in a LATER pipeline stage
    // (e.g. Step E stitching) does not discard the Step-D measurements we came
    // for. Not async-signal-safe in the strict sense, but adequate for this
    // scratch instrumentation: we stop the sampler, write the CSVs without
    // locking, then re-raise the signal with the default handler so the process
    // still dies with its real exit code (e.g. 139 for SIGSEGV).
    static void install_crash_handlers() {
        std::signal(SIGSEGV, &Profiler::on_crash);
        std::signal(SIGABRT, &Profiler::on_crash);
        std::signal(SIGBUS, &Profiler::on_crash);
        std::signal(SIGFPE, &Profiler::on_crash);
        // Also dump on termination/cancellation (e.g. SLURM walltime sends
        // SIGTERM before SIGKILL; scancel sends SIGTERM) so partial Step-D data
        // survives. SIGKILL cannot be caught.
        std::signal(SIGTERM, &Profiler::on_crash);
        std::signal(SIGINT, &Profiler::on_crash);
    }
    static void on_crash(int sig) {
        Profiler& p = I();
        p.stop_.store(true);
        if (!p.dumped_.exchange(true)) {
            const std::string out = p.out_dir();
            p.write_phases(out + "/xprof_phases.csv");
            p.write_events(out + "/xprof_events.csv");
            p.write_samples(out + "/xprof_samples.csv");
            std::fprintf(stderr, "\n[XPROF] emergency dump on signal %d -> %s\n", sig, out.c_str());
        }
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
    ~Profiler() {
        stop_.store(true);
        if (sampler_.joinable()) sampler_.join();
    }

    std::string out_dir() const {
        const char* e = std::getenv("XDIC_PROFILE_OUT");
        std::string d = e ? e : "xprof_out";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }

    void write_phases(const std::string& path) {
        std::ofstream f(path);
        if (!f) return;
        f << "phase,count,total_ms,peak_rss_bytes\n";
        for (const auto& kv : phases_)
            f << kv.first << ',' << kv.second.count << ',' << kv.second.total_ms << ','
              << kv.second.rss_peak << '\n';
    }
    void write_events(const std::string& path) {
        std::ofstream f(path);
        if (!f) return;
        f << "t_ms,label,rss_bytes\n";
        for (const auto& e : events_) f << e.t_ms << ',' << e.label << ',' << e.rss << '\n';
    }
    void write_samples(const std::string& path) {
        std::ofstream f(path);
        if (!f) return;
        f << "t_ms,rss_bytes\n";
        for (const auto& s : samples_) f << s.t_ms << ',' << s.rss << '\n';
    }

    bool enabled_ = false;
    clock::time_point t0_;
    std::mutex m_;
    std::map<std::string, Phase> phases_;
    std::vector<Event> events_;
    std::vector<Sample> samples_;
    std::thread sampler_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> dumped_{false};
};

struct ScopedTimer {
    std::string label;
    Profiler::clock::time_point s;
    explicit ScopedTimer(std::string l) : label(std::move(l)), s(Profiler::clock::now()) {
        Profiler::I().enter(label);
    }
    ~ScopedTimer() {
        double ms = std::chrono::duration<double, std::milli>(Profiler::clock::now() - s).count();
        Profiler::I().record(label, ms);
    }
};

} // namespace xprof

#define XPROF_CONCAT_(a, b) a##b
#define XPROF_CONCAT(a, b) XPROF_CONCAT_(a, b)
#define XPROF_SCOPE(name) ::xprof::ScopedTimer XPROF_CONCAT(_xprof_, __LINE__)(name)
