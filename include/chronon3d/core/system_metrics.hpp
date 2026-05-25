#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace chronon3d {

/**
 * SystemMetricsCollector — gathers OS-level metrics for telemetry.
 *
 * Collects:
 *  - FFmpeg child process CPU% (user + sys) via /proc/[pid]/stat
 *  - Page faults (major + minor) via /proc/[pid]/stat
 *  - Voluntary/involuntary context switches via /proc/[pid]/stat
 *  - LLC references/misses via perf_event_open (hardware PMU)
 *
 * On non-Linux platforms all methods are no-ops.
 */
class SystemMetricsCollector {
public:
    SystemMetricsCollector();

    // Non-copyable, non-movable (holds fd)
    SystemMetricsCollector(const SystemMetricsCollector&) = delete;
    SystemMetricsCollector& operator=(const SystemMetricsCollector&) = delete;
    SystemMetricsCollector(SystemMetricsCollector&&) = delete;
    SystemMetricsCollector& operator=(SystemMetricsCollector&&) = delete;

    ~SystemMetricsCollector();

    // ── FFmpeg child process CPU% ──────────────────────────────────────────
    // Pass the child process PID. On Linux, reads /proc/[pid]/stat on each call
    // and computes delta CPU% between samples.
    void track_ffmpeg_pid(int pid);
    void clear_ffmpeg_pid();

    struct CpuSplit {
        double user_pct{0.0};
        double sys_pct{0.0};
    };
    [[nodiscard]] CpuSplit ffmpeg_cpu_split() const;

    // ── Page faults & context switches (current process) ───────────────────
    struct ProcessMetrics {
        uint64_t voluntary_cs{0};
        uint64_t involuntary_cs{0};
        uint64_t major_faults{0};
        uint64_t minor_faults{0};
    };
    [[nodiscard]] ProcessMetrics process_metrics();

    // ── LLC counters via perf_event_open ───────────────────────────────────
    struct CacheMetrics {
        uint64_t references{0};
        uint64_t misses{0};
    };
    [[nodiscard]] CacheMetrics cache_metrics();

    // Read all current values into a counters struct (expects same-layout atomic fields)
    template <typename Counters>
    void fill_system_counters(Counters& c) {
        const auto cpu = ffmpeg_cpu_split();
        c.ffmpeg_cpu_user_pct.store(static_cast<uint64_t>(cpu.user_pct * 100.0 + 0.5), std::memory_order_relaxed);
        c.ffmpeg_cpu_sys_pct.store(static_cast<uint64_t>(cpu.sys_pct * 100.0 + 0.5), std::memory_order_relaxed);

        const auto pm = process_metrics();
        c.process_context_switches_voluntary.store(pm.voluntary_cs, std::memory_order_relaxed);
        c.process_context_switches_involuntary.store(pm.involuntary_cs, std::memory_order_relaxed);
        c.os_page_faults_major.store(pm.major_faults, std::memory_order_relaxed);
        c.os_page_faults_minor.store(pm.minor_faults, std::memory_order_relaxed);

        const auto cm = cache_metrics();
        c.llc_references.store(cm.references, std::memory_order_relaxed);
        c.llc_misses.store(cm.misses, std::memory_order_relaxed);
    }

private:
#ifdef __linux__
    int ffmpeg_pid_{-1};

    // For CPU% delta calculation (mutable so const ffmpeg_cpu_split can update them)
    mutable uint64_t prev_ffmpeg_utime_{0};
    mutable uint64_t prev_ffmpeg_stime_{0};
    mutable uint64_t prev_ffmpeg_clock_ticks_{0};

    // perf_event_open fds for LLC counters
    int fd_llc_ref_{-1};
    int fd_llc_miss_{-1};

    bool open_llc_counters();
    void close_llc_counters();
#endif
};

} // namespace chronon3d