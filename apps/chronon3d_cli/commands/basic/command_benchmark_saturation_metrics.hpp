#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::cli::benchmark_detail {

struct ProcStatusMetrics {
    std::uint64_t voluntary_cs{0};
    std::uint64_t involuntary_cs{0};
    std::uint64_t peak_rss_kb{0};
};

struct PerfMetrics {
    std::string cycles_per_frame{"N/A"};
    std::string instructions_per_frame{"N/A"};
    std::string ipc{"N/A"};
    std::string branch_miss_pct{"N/A"};
    std::string cache_miss_pct{"N/A"};
    std::string available{"false"};
};

[[nodiscard]] ProcStatusMetrics read_proc_status();
[[nodiscard]] double read_process_cpu_seconds();
[[nodiscard]] std::string read_cpu_model();
[[nodiscard]] int count_numa_nodes();
[[nodiscard]] PerfMetrics try_perf_stat(const std::string& cli_path,
                                        const std::string& scene,
                                        int frames, int warmup);
[[nodiscard]] double percentile(const std::vector<double>& sorted, double pct);

} // namespace chronon3d::cli::benchmark_detail
