#include <chronon3d/core/system_metrics.hpp>

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#endif

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

long sys_perf_event_open(struct perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

/**
 * Parse /proc/[pid]/stat — format:
 *   pid (comm) state ppid pgrp session tty_nr tpgid flags
 *   minflt cminflt majflt cmajflt utime stime cutime cstime ...
 *
 * comm may contain '(' ')' and spaces, so we find the LAST ')' to locate
 * the end of the comm field, then count space-separated fields from there.
 *
 * Fields we want (0-indexed after comm closing paren):
 *   7 = minflt, 9 = majflt, 11 = utime, 12 = stime
 */
void parse_proc_stat(const char* buf,
                     uint64_t& utime, uint64_t& stime,
                     uint64_t& minor_faults, uint64_t& major_faults) {
    utime = stime = minor_faults = major_faults = 0;

    const char* last_paren = nullptr;
    for (const char* p = buf; *p; ++p) {
        if (*p == ')') last_paren = p;
    }
    if (!last_paren) return;

    const char* p = last_paren + 1;
    while (*p == ' ') ++p;

    auto skip_field = [&]() {
        while (*p && *p != ' ') ++p;
        while (*p == ' ') ++p;
    };

    auto read_uint64 = [&](uint64_t& out) -> bool {
        while (*p == ' ') ++p;
        char* end = nullptr;
        out = std::strtoull(p, &end, 10);
        if (end == p) return false;
        p = end;
        while (*p == ' ') ++p;
        return true;
    };

    // Skip state(0), ppid(1), pgrp(2), session(3), tty_nr(4), tpgid(5), flags(6), minflt(7)
    for (int i = 0; i < 8; ++i) skip_field();
    if (!*p) return;

    if (!read_uint64(minor_faults)) return;  // minflt
    skip_field();                             // cminflt
    if (!read_uint64(major_faults)) return;  // majflt
    skip_field();                             // cmajflt
    if (!read_uint64(utime)) return;          // utime
    if (!read_uint64(stime)) return;          // stime
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
}

SystemMetricsCollector::~SystemMetricsCollector() {
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

SystemMetricsCollector::CpuSplit SystemMetricsCollector::ffmpeg_cpu_split() const {
    CpuSplit result;
    if (ffmpeg_pid_ < 0) return result;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", ffmpeg_pid_);

    std::ifstream stat_file{path};
    if (!stat_file) return result;

    std::string buf;
    std::getline(stat_file, buf);
    if (buf.empty()) return result;

    uint64_t utime = 0, stime = 0, minor = 0, major = 0;
    parse_proc_stat(buf.c_str(), utime, stime, minor, major);

    if (utime == 0 && stime == 0) return result;

    // Use the first call to prime the previous values
    if (prev_ffmpeg_clock_ticks_ == 0) {
        prev_ffmpeg_utime_ = utime;
        prev_ffmpeg_stime_ = stime;
        prev_ffmpeg_clock_ticks_ = utime + stime;
        return result;
    }

    const uint64_t delta_utime = (utime > prev_ffmpeg_utime_) ? (utime - prev_ffmpeg_utime_) : 0;
    const uint64_t delta_stime = (stime > prev_ffmpeg_stime_) ? (stime - prev_ffmpeg_stime_) : 0;
    const uint64_t delta_total_jiffies = delta_utime + delta_stime;

    // We don't track wall time between calls here — use a nominal 100ms sample window
    // for the percentage calculation. This is an approximation; for accurate CPU%
    // the caller should provide the elapsed time via a separate mechanism or sample
    // at a known interval (e.g. every 1 second).
    constexpr double kSampleWindowSec = 0.1; // assumed 100ms between calls
    if (delta_total_jiffies > 0) {
        const double clk_tck = static_cast<double>(clock_ticks_per_sec());
        result.user_pct = (static_cast<double>(delta_utime) / clk_tck) / kSampleWindowSec * 100.0;
        result.sys_pct  = (static_cast<double>(delta_stime) / clk_tck) / kSampleWindowSec * 100.0;
    }

    prev_ffmpeg_utime_ = utime;
    prev_ffmpeg_stime_ = stime;

    return result;
}

SystemMetricsCollector::ProcessMetrics SystemMetricsCollector::process_metrics() {
    ProcessMetrics pm;
    pm.minor_faults = 0;
    pm.major_faults = 0;

    // Read /proc/self/stat for page faults (minflt/majflt)
    std::ifstream stat_file{"/proc/self/stat"};
    if (stat_file) {
        std::string buf;
        std::getline(stat_file, buf);
        if (!buf.empty()) {
            uint64_t utime = 0, stime = 0;
            parse_proc_stat(buf.c_str(), utime, stime, pm.minor_faults, pm.major_faults);
        }
    }

    // Read /proc/self/status for context switches
    read_proc_status_cs(pm.voluntary_cs, pm.involuntary_cs);

    return pm;
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
static uint64_t s_peak_rss_bytes = 0;

SystemMetricsCollector::ProcessCpuTime SystemMetricsCollector::process_cpu_time() {
    ProcessCpuTime ct;
    std::ifstream stat_file{"/proc/self/stat"};
    if (stat_file) {
        std::string buf;
        std::getline(stat_file, buf);
        if (!buf.empty()) {
            uint64_t minor = 0, major = 0;
            parse_proc_stat(buf.c_str(), ct.utime_jiffies, ct.stime_jiffies, minor, major);
        }
    }
    return ct;
}

uint64_t SystemMetricsCollector::process_rss_bytes() {
    std::ifstream statm{"/proc/self/statm"};
    if (!statm) return 0;
    uint64_t pages = 0, resident = 0;
    statm >> pages >> resident;
    const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t rss = resident * static_cast<uint64_t>(page_size > 0 ? page_size : 4096);
    if (rss > s_peak_rss_bytes) s_peak_rss_bytes = rss;
    return s_peak_rss_bytes;
}

void SystemMetricsCollector::sample_cpu_start() {
    const auto ct = process_cpu_time();
    baseline_utime_ = ct.utime_jiffies;
    baseline_stime_ = ct.stime_jiffies;
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

SystemMetricsCollector::SystemMemory SystemMetricsCollector::system_memory() {
    SystemMemory mem;
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo) return mem;
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.compare(0, 9, "MemTotal:") == 0) {
            std::istringstream iss(line.substr(9));
            uint64_t kb = 0;
            iss >> kb;
            mem.total_bytes = kb * 1024ULL;
        } else if (line.compare(0, 12, "MemAvailable:") == 0) {
            std::istringstream iss(line.substr(12));
            uint64_t kb = 0;
            iss >> kb;
            mem.available_bytes = kb * 1024ULL;
        }
    }
    return mem;
}

#endif // __linux__

} // namespace chronon3d