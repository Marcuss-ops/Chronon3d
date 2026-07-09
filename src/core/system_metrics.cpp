#include <chronon3d/core/system_metrics.hpp>

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#endif

#include "system_metrics_parse.hpp"

namespace chronon3d {

#ifndef __linux__

SystemMetricsCollector::SystemMetricsCollector() = default;
SystemMetricsCollector::~SystemMetricsCollector() = default;
SystemMetricsCollector::CpuSplit SystemMetricsCollector::ffmpeg_cpu_split() const { return {}; }
SystemMetricsCollector::ProcessMetrics SystemMetricsCollector::process_metrics() { return {}; }
SystemMetricsCollector::CacheMetrics SystemMetricsCollector::cache_metrics() { return {}; }
SystemMetricsCollector::ProcessCpuTime SystemMetricsCollector::process_cpu_time() { return {}; }
uint64_t SystemMetricsCollector::process_rss_bytes() { return 0; }
SystemMetricsCollector::SystemMemory SystemMetricsCollector::system_memory() { return {}; }
long SystemMetricsCollector::clock_ticks_per_sec() { return 100; }
void SystemMetricsCollector::track_ffmpeg_pid(int) {}
void SystemMetricsCollector::clear_ffmpeg_pid() {}
void SystemMetricsCollector::sample_cpu_start() {}
SystemMetricsCollector::ProcessCpuTime SystemMetricsCollector::sample_cpu_delta() { return {}; }

#else

namespace {

using chronon3d::detail::parse_proc_stat;

long sys_perf_event_open(struct perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

/**
 * Read Voluntary_ctxt_switches and NonVoluntary_ctxt_switches from /proc/self/status.
 */
void read_proc_status_cs(uint64_t& vol_cs, uint64_t& invol_cs) {
    vol_cs = invol_cs = 0;
    std::ifstream f{"/proc/self/status"};
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 22 && line.compare(0, 22, "Voluntary_ctxt_switches") == 0) {
            std::istringstream iss(line.substr(22));
            iss >> vol_cs;
        }
        if (line.size() > 25 && line.compare(0, 25, "NonVoluntary_ctxt_switches") == 0) {
            std::istringstream iss(line.substr(25));
            iss >> invol_cs;
        }
    }
}

} // anonymous namespace

SystemMetricsCollector::SystemMetricsCollector() {
    ffmpeg_pid_ = -1;
    prev_ffmpeg_utime_ = 0;
    prev_ffmpeg_stime_ = 0;
    prev_ffmpeg_clock_ticks_ = 0;
    fd_llc_ref_ = -1;
    fd_llc_miss_ = -1;
    baseline_utime_ = 0;
    baseline_stime_ = 0;
    baseline_valid_ = false;
    open_llc_counters();
    // TICKET-122 -- start the async /proc reads worker thread.
    // The worker is joined in the destructor before close_llc_counters()
    // so fd lifetime is bounded strictly by the worker's existence.
    m_worker = std::thread(&SystemMetricsCollector::tick_worker, this);
}

SystemMetricsCollector::~SystemMetricsCollector() {
    // TICKET-122 -- graceful worker shutdown.  Setting the stop flag tells
    // the worker to exit its sleep loop on the next iteration; join()
    // blocks until that completes (max ~1s by design).  Release-store
    // pairs with the worker's acquire-load so the worker sees the flag
    // by its next iteration check.
    m_stop.store(true, std::memory_order_release);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    close_llc_counters();
}

bool SystemMetricsCollector::open_llc_counters() {
    struct perf_event_attr pe{};
    std::memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(struct perf_event_attr);
    pe.exclude_kernel = 0;
    pe.exclude_hv = 1;
    pe.disabled = 1;

    pe.config = PERF_COUNT_HW_CACHE_REFERENCES;  // 0x80000000 on Linux
    fd_llc_ref_ = static_cast<int>(sys_perf_event_open(&pe, -1, 0, -1, 0));
    if (fd_llc_ref_ >= 0) {
        ioctl(fd_llc_ref_, PERF_EVENT_IOC_ENABLE, 0);
    }

    pe.config = PERF_COUNT_HW_CACHE_MISSES;  // 0x80000001 on Linux
    fd_llc_miss_ = static_cast<int>(sys_perf_event_open(&pe, -1, 0, -1, 0));
    if (fd_llc_miss_ >= 0) {
        ioctl(fd_llc_miss_, PERF_EVENT_IOC_ENABLE, 0);
    }

    return fd_llc_ref_ >= 0 && fd_llc_miss_ >= 0;
}

void SystemMetricsCollector::close_llc_counters() {
    if (fd_llc_ref_ >= 0) { ::close(fd_llc_ref_); fd_llc_ref_ = -1; }
    if (fd_llc_miss_ >= 0) { ::close(fd_llc_miss_); fd_llc_miss_ = -1; }
}

void SystemMetricsCollector::track_ffmpeg_pid(int pid) {
    ffmpeg_pid_ = pid;
    prev_ffmpeg_utime_ = 0;
    prev_ffmpeg_stime_ = 0;
    prev_ffmpeg_clock_ticks_ = 0;
}

void SystemMetricsCollector::clear_ffmpeg_pid() {
    ffmpeg_pid_ = -1;
}

// TICKET-122 -- ffmpeg_cpu_split now serves cached values from the
// background worker's last successful read.  This decouples the
// RenderEngine hot-path fill_system_counters from sync /proc/[pid]/stat.
SystemMetricsCollector::CpuSplit SystemMetricsCollector::ffmpeg_cpu_split() const {
    CpuSplit result;
    // TICKET-122 -- acquire-load pairs with the worker's release-store
    // so the caller sees the cache values coherently across the two
    // user/sys halves (no torn-read window).
    result.user_pct = m_cached_ffmpeg_user_pct.load(std::memory_order_acquire);
    result.sys_pct  = m_cached_ffmpeg_sys_pct.load(std::memory_order_acquire);
    return result;
}

// TICKET-122 -- process_metrics serves cached values for all 4 fields
// (page faults + context switches).  No synchronous /proc/self/* read.
// Acquire loads pair with the worker's release-stores so all 4 fields
// reflect a coherent worker-tick snapshot rather than a torn mix.
SystemMetricsCollector::ProcessMetrics SystemMetricsCollector::process_metrics() {
    return ProcessMetrics{
        m_cached_voluntary_cs.load(std::memory_order_acquire),
        m_cached_involuntary_cs.load(std::memory_order_acquire),
        m_cached_major_faults.load(std::memory_order_acquire),
        m_cached_minor_faults.load(std::memory_order_acquire)
    };
}

SystemMetricsCollector::CacheMetrics SystemMetricsCollector::cache_metrics() {
    CacheMetrics cm;
    if (fd_llc_ref_ >= 0) {
        if (::read(fd_llc_ref_, &cm.references, sizeof(cm.references)) != sizeof(cm.references)) {
            cm.references = 0;
        }
    }
    if (fd_llc_miss_ >= 0) {
        if (::read(fd_llc_miss_, &cm.misses, sizeof(cm.misses)) != sizeof(cm.misses)) {
            cm.misses = 0;
        }
    }
    return cm;
}

long SystemMetricsCollector::clock_ticks_per_sec() {
    static const long ticks = static_cast<long>(sysconf(_SC_CLK_TCK));
    return ticks > 0 ? ticks : 100;
}

// TICKET-122 -- removed file-static `s_peak_rss_bytes`; the peak RSS
// is now tracked across worker ticks via m_cached_rss_bytes (CAS-based
// monotonic update).  Lifetime is bounded to the collector instance
// (vs. the previous process-wide file-static), which improves testability
// and avoids shared mutable state across collector instances.

// TICKET-122 -- acquire-load pairs with the worker's release-store so
// the caller sees both jiffies values from one coherent tick snapshot.
SystemMetricsCollector::ProcessCpuTime SystemMetricsCollector::process_cpu_time() {
    return ProcessCpuTime{
        m_cached_utime_jiffies.load(std::memory_order_acquire),
        m_cached_stime_jiffies.load(std::memory_order_acquire)
    };
}

uint64_t SystemMetricsCollector::process_rss_bytes() {
    // TICKET-122 -- acquire-load pairs with the worker's release-CAS.
    return m_cached_rss_bytes.load(std::memory_order_acquire);
}

void SystemMetricsCollector::sample_cpu_start() {
    // TICKET-122 -- baseline captures from the worker-populated cache
    // (instead of conducting a sync read itself).  Up to 1s staleness.
    // Acquire so the baseline reflects the worker's last release-store.
    baseline_utime_ = m_cached_utime_jiffies.load(std::memory_order_acquire);
    baseline_stime_ = m_cached_stime_jiffies.load(std::memory_order_acquire);
    baseline_valid_ = true;
}

SystemMetricsCollector::ProcessCpuTime SystemMetricsCollector::sample_cpu_delta() {
    ProcessCpuTime delta;
    if (!baseline_valid_) return delta;
    const auto ct = process_cpu_time();
    const uint64_t d_utime = (ct.utime_jiffies > baseline_utime_)
        ? (ct.utime_jiffies - baseline_utime_) : 0;
    const uint64_t d_stime = (ct.stime_jiffies > baseline_stime_)
        ? (ct.stime_jiffies - baseline_stime_) : 0;
    const long clk = clock_ticks_per_sec();
    delta.utime_jiffies = d_utime;
    delta.stime_jiffies = d_stime;
    return delta;
}

// TICKET-122 -- acquire-load pairs with the worker's release-store so
// both system-memory halves reflect one coherent worker tick.
SystemMetricsCollector::SystemMemory SystemMetricsCollector::system_memory() {
    return SystemMemory{
        m_cached_sys_mem_total_bytes.load(std::memory_order_acquire),
        m_cached_sys_mem_available_bytes.load(std::memory_order_acquire)
    };
}

// ── TICKET-122 — worker entry + full /proc self+pid read sweep ────────────
//
// Both methods run on the dedicated worker m_worker (constructed in
// the ctor).  tick_worker() drives a 1-second wake cadence and calls
// update_caches(), which performs the actual reads and stores into
// the atomic caches consumed by the public accessors above.
//
// Cadence: 1 Hz.  Trade-off: callers see values up to ~1 second stale.
// Net win: the RenderEngine hot-path subscription eliminates up to
// five synchronous /proc reads per snapshot.
void SystemMetricsCollector::tick_worker() {
    // Initial populate before sleeping, so the first call into
    // process_metrics()/system_memory()/etc. observes non-zero values
    // (modulo Linux kernel reporting).
    update_caches();
    // TICKET-122 -- acquire-load the stop flag so any prior
    // release-store from ~SystemMetricsCollector is visible to the
    // worker thread (shutdown is bounded to <1s in the worst case).
    while (!m_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        update_caches();
    }
}

void SystemMetricsCollector::update_caches() {
    // 1) Voluntary / involuntary context switches  --  /proc/self/status
    {
        uint64_t vol_cs = 0, invol_cs = 0;
        read_proc_status_cs(vol_cs, invol_cs);
        // TICKET-122 -- release-store pairs with the consumer's
        // acquire-load so both halves reflect the same tick snapshot.
        m_cached_voluntary_cs.store(vol_cs, std::memory_order_release);
        m_cached_involuntary_cs.store(invol_cs, std::memory_order_release);
    }

    // 2) Page faults (major / minor) AND process CPU jiffies
    //    (utime / stime) --  /proc/self/stat  (single read, both
    //    update_caches_steps share one file open).
    {
        std::ifstream stat_file{"/proc/self/stat"};
        if (stat_file) {
            std::string buf;
            std::getline(stat_file, buf);
            if (!buf.empty()) {
                uint64_t utime = 0, stime = 0, minor = 0, major = 0;
                parse_proc_stat(buf.c_str(), utime, stime, minor, major);
                // TICKET-122 -- release-stores pair with consumer
                // acquire-loads so a downstream reader sees all 4
                // values coherently from the same tick.
                m_cached_major_faults.store(major, std::memory_order_release);
                m_cached_minor_faults.store(minor, std::memory_order_release);
                m_cached_utime_jiffies.store(utime, std::memory_order_release);
                m_cached_stime_jiffies.store(stime, std::memory_order_release);
            }
        }
    }

    // 3) Process RSS (peak, monotonic across ticks)  --  /proc/self/statm
    //
    // Monotonic peak via CAS retry on m_cached_rss_bytes.  Safe under
    // concurrent reads: any reader that wins the race after the worker
    // publishes a higher value will see the new peak on the next tick.
    {
        uint64_t rss = 0;
        std::ifstream statm{"/proc/self/statm"};
        if (statm) {
            uint64_t pages = 0, resident = 0;
            statm >> pages >> resident;
            const long page_size = sysconf(_SC_PAGESIZE);
            rss = resident * static_cast<uint64_t>(page_size > 0 ? page_size : 4096);
        }
        uint64_t current = m_cached_rss_bytes.load(std::memory_order_acquire);
        while (rss > current) {
            // TICKET-122 -- release on success pairs with consumer
            // acquire-load; relaxed on failure path (CAS retry is
            // racy-but-monotonic, the next iteration will re-read).
            if (m_cached_rss_bytes.compare_exchange_weak(current, rss,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed)) {
                break;
            }
        }
    }

    // 4) System RAM (total / available)  --  /proc/meminfo
    {
        uint64_t total_bytes = 0, available_bytes = 0;
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo) {
            std::string line;
            while (std::getline(meminfo, line)) {
                if (line.compare(0, 9, "MemTotal:") == 0) {
                    std::istringstream iss(line.substr(9));
                    uint64_t kb = 0;
                    iss >> kb;
                    total_bytes = kb * 1024ULL;
                } else if (line.compare(0, 13, "MemAvailable:") == 0) {
                    std::istringstream iss(line.substr(13));
                    uint64_t kb = 0;
                    iss >> kb;
                    available_bytes = kb * 1024ULL;
                }
            }
        }
        // TICKET-122 -- release-stores pair with consumer acquires.
        m_cached_sys_mem_total_bytes.store(total_bytes, std::memory_order_release);
        m_cached_sys_mem_available_bytes.store(available_bytes, std::memory_order_release);
    }

    // 5) FFmpeg child process CPU%  --  /proc/[pid]/stat
    //
    // Same delta algorithm as the pre-ticker implementation, but the
    // sample window is now 1.0 second (matching the ticker cadence)
    // instead of the previously assumed 0.1 second.  prev_ffmpeg_*
    // remain mutable so a const ffmpeg_cpu_split call still observes
    // the latest deltas via the cache.
    {
        CpuSplit result;
        if (ffmpeg_pid_ >= 0) {
            char path[64];
            snprintf(path, sizeof(path), "/proc/%d/stat", ffmpeg_pid_);
            std::ifstream stat_file{path};
            if (stat_file) {
                std::string buf;
                std::getline(stat_file, buf);
                if (!buf.empty()) {
                    uint64_t utime = 0, stime = 0, minor = 0, major = 0;
                    parse_proc_stat(buf.c_str(), utime, stime, minor, major);
                    if (utime != 0 || stime != 0) {
                        if (prev_ffmpeg_clock_ticks_ == 0) {
                            // First successful read after construction /
                            // track_ffmpeg_pid(): prime the previous
                            // values; the cached CpuSplit on this tick
                            // is zero (acceptable -- next tick will
                            // produce a delta).
                            prev_ffmpeg_utime_ = utime;
                            prev_ffmpeg_stime_ = stime;
                            prev_ffmpeg_clock_ticks_ = utime + stime;
                        } else {
                            const uint64_t d_utime = (utime > prev_ffmpeg_utime_) ? (utime - prev_ffmpeg_utime_) : 0;
                            const uint64_t d_stime = (stime > prev_ffmpeg_stime_) ? (stime - prev_ffmpeg_stime_) : 0;
                            const uint64_t d_total = d_utime + d_stime;
                            // Sample window now equals the ticker freq
                            // (so the worker's per-second delta matches
                            //  the CPU% semantics callers expect).
                            constexpr double kSampleWindowSec = 1.0;
                            if (d_total > 0) {
                                const double clk = static_cast<double>(clock_ticks_per_sec());
                                result.user_pct = (static_cast<double>(d_utime) / clk) / kSampleWindowSec * 100.0;
                                result.sys_pct  = (static_cast<double>(d_stime) / clk) / kSampleWindowSec * 100.0;
                            }
                            prev_ffmpeg_utime_ = utime;
                            prev_ffmpeg_stime_ = stime;
                        }
                    }
                }
            }
        }
        // TICKET-122 -- release-stores pair with consumer acquires so
        // the caller sees both halves coherently.
        m_cached_ffmpeg_user_pct.store(result.user_pct, std::memory_order_release);
        m_cached_ffmpeg_sys_pct.store(result.sys_pct, std::memory_order_release);
    }
}

#endif // __linux__

} // namespace chronon3d
