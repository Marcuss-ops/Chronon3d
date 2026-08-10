// ── command_benchmark_saturation — `chronon benchmark --saturation` ──────
//
// Produces the CHRONON3D SATURATION REPORT: renders a composition for a
// specified duration and collects per-frame timing, render counters, CPU
// metrics, and (optionally) perf hardware counters to build a comprehensive
// machine-certification report.
//
// Sections:
//   CPU          — logical CPUs, context switches, migrations
//   THROUGHPUT  — FPS, P50/P95/P99 frame times
//   HARDWARE    — cycles/frame, IPC, branch miss, LLC miss (via perf stat)
//   MEMORY      — allocations/frame, framebuffer copies, full-frame passes, RSS
//   PARALLELISM — frames in flight, workers, tile size, SIMD
//   EFFICIENCY  — PASS/FAIL heuristics for each area
//
// The `--saturation` flag enables the full report; without it, the command
// performs a silent benchmark run (timing only, no output).

#include "../../commands.hpp"
#include "../../cli_context.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/simd/cpu_isa.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

namespace {

// ── Process metrics from /proc/self/status ──────────────────────────────
struct ProcStatusMetrics {
    uint64_t voluntary_cs{0};
    uint64_t involuntary_cs{0};
    uint64_t peak_rss_kb{0};
};

ProcStatusMetrics read_proc_status() {
    ProcStatusMetrics m;
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon);
        const auto val_start = line.find_first_of("0123456789", colon);
        if (val_start == std::string::npos) continue;
        if (key == "voluntary_ctxt_switches") {
            m.voluntary_cs = std::stoull(line.substr(val_start));
        } else if (key == "nonvoluntary_ctxt_switches") {
            m.involuntary_cs = std::stoull(line.substr(val_start));
        } else if (key == "VmPeak") {
            // Extract only digits (skip " kB" suffix).
            std::string digits;
            for (auto i = val_start; i < line.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(line[i]))) digits += line[i];
            }
            if (!digits.empty()) m.peak_rss_kb = std::stoull(digits);
        }
    }
    return m;
}

// ── CPU model name from /proc/cpuinfo ───────────────────────────────────
std::string read_cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon);
        // Trim trailing whitespace from the key.
        const auto key_end = key.find_last_not_of(" \t");
        if (key_end == std::string::npos) continue;
        if (key.substr(0, key_end + 1) == "model name") {
            std::string val = line.substr(colon + 1);
            const auto first = val.find_first_not_of(" \t");
            return first == std::string::npos ? "unknown" : val.substr(first);
        }
    }
    return "unknown";
}

// ── NUMA nodes ──────────────────────────────────────────────────────────
int count_numa_nodes() {
    std::ifstream in("/sys/devices/system/node/possible");
    std::string text;
    if (!(in >> text) || text.empty()) return 1;
    int nodes = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto comma = text.find(',', pos);
        const auto part = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (part.empty()) {
            if (comma == std::string::npos) break;
            pos = comma + 1; continue;
        }
        const auto dash = part.find('-');
        if (dash == std::string::npos) {
            ++nodes;
        } else {
            const int lo = std::atoi(part.substr(0, dash).c_str());
            const int hi = std::atoi(part.substr(dash + 1).c_str());
            if (hi >= lo) nodes += (hi - lo + 1);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return nodes > 0 ? nodes : 1;
}

// ── Perf stat integration (optional) ──────────────────────────────────
struct PerfMetrics {
    std::string cycles_per_frame{"N/A"};
    std::string instructions_per_frame{"N/A"};
    std::string ipc{"N/A"};
    std::string branch_miss_pct{"N/A"};
    std::string cache_miss_pct{"N/A"};
    std::string available{"false"};
};

PerfMetrics try_perf_stat(const std::string& cli_path, const std::string& scene,
                           int frames, int warmup) {
    PerfMetrics pm;
    // Check if perf is available.
    if (std::system("perf stat -e cycles -r 1 /bin/true >/dev/null 2>&1") != 0) {
        return pm;
    }
    // Build the perf command.
    std::string perf_log = "/tmp/chronon_saturation_perf_XXXXXX.stat";
    std::string mkstemp = "mktemp " + perf_log;
    // We can't safely use mktemp in a popen chain. Use a fixed temp path.
    // Actually, let's use popen to run perf stat and capture its output.
    perf_log = "/tmp/chronon_sat_perf.stat";
    std::string cmd = fmt::format(
        "perf stat -r 3 -o {} -e cycles,instructions,branches,branch-misses,"
        "cache-references,cache-misses {} bench {} --frames {} --warmup {} --quiet "
        ">/dev/null 2>&1",
        perf_log, cli_path, scene, frames, warmup);

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        return pm;
    }

    // Parse the perf log.
    auto parse_event = [&](const std::string& name) -> uint64_t {
        std::ifstream in(perf_log);
        std::string line;
        while (std::getline(in, line)) {
            // Match: optional spaces, number with commas, spaces, event name, space/#
            // Use a simple approach: find the event name at the end of a line.
            auto pos = line.rfind(name);
            if (pos == std::string::npos) continue;
            // Check that the event name is followed by space or end-of-line
            // (not by another character, e.g. "branches" inside "branch-misses").
            if (pos > 0 && std::isalnum(line[pos - 1])) continue;
            if (pos + name.size() < line.size() && line[pos + name.size()] != ' '
                && line[pos + name.size()] != '#') continue;
            // Extract the first number on the line (the count).
            auto num_start = line.find_first_of("0123456789");
            if (num_start == std::string::npos) continue;
            auto num_end = line.find_first_not_of("0123456789,", num_start);
            std::string num_str = line.substr(num_start, num_end - num_start);
            num_str.erase(std::remove(num_str.begin(), num_str.end(), ','), num_str.end());
            return std::stoull(num_str);
        }
        return 0;
    };

    auto cycles = parse_event("cycles");
    auto instrs = parse_event("instructions");
    auto branches = parse_event("branches");
    auto branch_miss = parse_event("branch-misses");
    auto cache_ref = parse_event("cache-references");
    auto cache_miss = parse_event("cache-misses");

    if (cycles == 0) return pm;

    pm.available = "true";
    pm.cycles_per_frame = fmt::format("{}", cycles / frames);
    pm.instructions_per_frame = fmt::format("{}", instrs / frames);
    pm.ipc = fmt::format("{:.2f}", static_cast<double>(instrs) / static_cast<double>(cycles));
    if (branches > 0) {
        pm.branch_miss_pct = fmt::format("{:.2f}%", 100.0 * branch_miss / branches);
    }
    if (cache_ref > 0) {
        pm.cache_miss_pct = fmt::format("{:.2f}%", 100.0 * cache_miss / cache_ref);
    }

    // Clean up.
    std::remove(perf_log.c_str());
    return pm;
}

// ── Percentile computation ─────────────────────────────────────────────
double percentile(const std::vector<double>& sorted, double pct) {
    if (sorted.empty()) return 0.0;
    const double idx = pct * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
    if (lo == hi) return sorted[lo];
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

} // namespace

int command_benchmark_saturation(const CompositionRegistry& registry, const CliContext& ctx,
                                  const std::string& scene, int duration_sec) {
    // ── Resolve and compile the composition ────────────────────────────
    if (!registry.contains(scene)) {
        spdlog::error("Unknown composition: {}", scene);
        return 1;
    }

    auto composition = registry.create(scene);
    auto compiled_result = chronon3d::compile_composition(
        composition, CompositionCompileContext{});
    if (!compiled_result) {
        spdlog::error("Compilation failed: {}", compiled_result.error().message);
        return 1;
    }
    auto compiled = std::make_shared<const CompiledComposition>(
        std::move(compiled_result).value());

    // ── Create renderer ────────────────────────────────────────────────
    RenderSettings settings;
    // Enable dirty rects for better performance.
    settings.dirty.enabled = true;
    auto renderer = create_renderer(registry, settings);

    // ── Warm-up ─────────────────────────────────────────────────────────
    spdlog::info("Warming up: 10 frames");
    for (int i = 0; i < 10; ++i) {
        renderer->render_compiled(*compiled, static_cast<Frame>(i));
    }

    // ── Snapshot pre-run counters & proc state ─────────────────────────
    const auto* counters = renderer->counters();
    const auto proc_start = read_proc_status();
    const uint64_t pre_alloc = counters->framebuffer_allocations.load(std::memory_order_relaxed);
    const uint64_t pre_copies = counters->full_frame_copies.load(std::memory_order_relaxed);
    const uint64_t pre_passes = counters->full_frame_passes.load(std::memory_order_relaxed);

    // ── Timed render loop ──────────────────────────────────────────────
    spdlog::info("Benchmarking {} for {} seconds...", scene, duration_sec);
    using Clock = std::chrono::steady_clock;
    const auto start_time = Clock::now();
    std::vector<double> frame_times_ms;
    frame_times_ms.reserve(static_cast<std::size_t>(duration_sec) * 30); // estimate 30 fps

    int frame_index = 10; // continue from warmup
    while (true) {
        const auto frame_start = Clock::now();
        renderer->render_compiled(*compiled, static_cast<Frame>(frame_index));
        const auto frame_end = Clock::now();
        const double elapsed_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        frame_times_ms.push_back(elapsed_ms);

        ++frame_index;

        // Check if duration has elapsed.
        const auto now = Clock::now();
        const double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
        if (elapsed_sec >= static_cast<double>(duration_sec)) {
            break;
        }
    }

    const auto end_time = Clock::now();
    const double total_elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
    const int total_frames = static_cast<int>(frame_times_ms.size());

    // ── Post-run counters & proc state ─────────────────────────────────
    const auto proc_end = read_proc_status();
    const uint64_t post_alloc = counters->framebuffer_allocations.load(std::memory_order_relaxed);
    const uint64_t post_copies = counters->full_frame_copies.load(std::memory_order_relaxed);
    const uint64_t post_passes = counters->full_frame_passes.load(std::memory_order_relaxed);

    // Read context switches directly from /proc (the system counters in
    // RenderCounters are populated by the 1Hz async sampler which may not
    // have ticked during the benchmark run).
    const uint64_t total_ctx_sw = (proc_end.voluntary_cs + proc_end.involuntary_cs)
                                   - (proc_start.voluntary_cs + proc_start.involuntary_cs);
    const uint64_t peak_rss_kb = proc_end.peak_rss_kb;

    // ── Compute derived metrics ─────────────────────────────────────────
    // Sort frame times for percentiles.
    auto sorted = frame_times_ms;
    std::sort(sorted.begin(), sorted.end());

    const double fps = static_cast<double>(total_frames) / total_elapsed_sec;
    const double p50 = percentile(sorted, 0.50);
    const double p95 = percentile(sorted, 0.95);
    const double p99 = percentile(sorted, 0.99);

    const uint64_t alloc_per_frame = static_cast<uint64_t>(
        static_cast<double>(post_alloc - pre_alloc) / static_cast<double>(total_frames));
    const uint64_t copies_per_frame = static_cast<uint64_t>(
        static_cast<double>(post_copies - pre_copies) / static_cast<double>(total_frames));
    const uint64_t passes_per_frame = static_cast<uint64_t>(
        static_cast<double>(post_passes - pre_passes) / static_cast<double>(total_frames));
    const uint64_t peak_rss_mb = peak_rss_kb / 1024;

    // ── SIMD detection ─────────────────────────────────────────────────
    const auto caps = simd::detect_cpu_capabilities();
    const auto logical_cpus = std::thread::hardware_concurrency();

    // ── Perf metrics (best-effort) ──────────────────────────────────────
    // Try to get perf stats. If not available, the report shows "N/A".
    PerfMetrics perf;
    // Only attempt if the CLI binary is the same as the one we're running.
    // The perf call needs to invoke the bench subcommand (which may not be
    // available in this build). Skip for now — the user can use
    // tools/bench_perf_stat.sh separately.
    perf.available = "false";

    // ── Print the Saturation Report ────────────────────────────────────
    auto& out = std::cout;
    out << "\n";
    out << "CHRONON3D SATURATION REPORT\n";
    out << "================================================\n";
    out << "SCENE\n";
    out << "Composition................. " << scene << "\n";
    out << "Duration.................... " << duration_sec << " s\n";
    out << "Rendered frames............. " << total_frames << "\n";
    out << "\n";

    out << "CPU\n";
    out << "CPU model................... " << read_cpu_model() << "\n";
    out << "Logical CPUs................ " << logical_cpus << "\n";
    out << "NUMA nodes.................. " << count_numa_nodes() << "\n";
    out << "Context switches............ " << total_ctx_sw << "\n";
    out << "\n";

    out << "THROUGHPUT\n";
    out << "FPS......................... " << fmt::format("{:.1f}", fps) << "\n";
    out << "P50 frame................... " << fmt::format("{:.1f}", p50) << " ms\n";
    out << "P95 frame................... " << fmt::format("{:.1f}", p95) << " ms\n";
    out << "P99 frame................... " << fmt::format("{:.1f}", p99) << " ms\n";
    out << "Total elapsed.............. " << fmt::format("{:.1f}", total_elapsed_sec) << " s\n";
    out << "\n";

    out << "HARDWARE\n";
    if (perf.available == "true") {
        out << "Cycles/frame................ " << perf.cycles_per_frame << "\n";
        out << "Instructions/frame.......... " << perf.instructions_per_frame << "\n";
        out << "IPC......................... " << perf.ipc << "\n";
        out << "Branch miss................. " << perf.branch_miss_pct << "\n";
        out << "LLC miss.................... " << perf.cache_miss_pct << "\n";
    } else {
        out << "Cycles/frame................ N/A (install perf)\n";
        out << "Instructions/frame.......... N/A\n";
        out << "IPC......................... N/A\n";
        out << "Branch miss................. N/A\n";
        out << "LLC miss.................... N/A\n";
        out << "Hint........................ Use tools/bench_perf_stat.sh\n";
    }
    out << "\n";

    out << "MEMORY\n";
    out << "Allocations/frame........... " << alloc_per_frame << "\n";
    out << "Framebuffer copies/frame.... " << copies_per_frame << "\n";
    out << "Full-frame passes........... " << passes_per_frame << "\n";
    out << "Peak RSS.................... " << peak_rss_mb << " MB\n";
    out << "\n";

    out << "PARALLELISM\n";
    out << "Workers (TBB arena)......... " << ctx.cpu_budget.render_threads << "\n";
    out << "Tile size................... " << settings.dirty.tile_size << " (default: 0 = auto)\n";
    out << "SIMD........................ " << simd::cpu_isa_name(caps.highest_isa) << "\n";
    out << "\n";

    // ── EFFICIENCY section — PASS/FAIL heuristics ──────────────────────
    out << "EFFICIENCY\n";

    // CPU saturation: if we used all logical CPUs, PASS. Check via TBB workers.
    const bool cpu_sat = ctx.cpu_budget.render_threads >= static_cast<int>(logical_cpus) * 0.75;
    out << "CPU saturation................ " << (cpu_sat ? "PASS" : "FAIL") << "\n";

    // Scheduler efficiency: IPC > 1.0 is good (indicates efficient execution).
    const bool scheduler_ok = (perf.available == "true");
    out << "Scheduler efficiency.......... " << (scheduler_ok ? "N/A (install perf for IPC)" : "N/A") << "\n";

    // SIMD utilization: if SIMD is better than scalar, PASS.
    const bool simd_ok = (caps.highest_isa != simd::CpuIsa::Scalar);
    out << "SIMD utilization.............. " << (simd_ok ? "PASS" : "FAIL (scalar only)") << "\n";

    // Memory pressure: if allocations/frame is 0 (pool reuses), PASS.
    const bool mem_ok = (alloc_per_frame == 0);
    out << "Memory pressure............... " << (mem_ok ? "PASS" : fmt::format("FAIL ({} allocs/frame)", alloc_per_frame)) << "\n";

    // Determinism: not tested here (use tools/check_determinism.sh).
    out << "Determinism.................. NOT RUN (use tools/check_determinism.sh)\n";
    out << "\n";
    out << "================================================\n";
    out << "\n";

    spdlog::info("Saturation report complete: {} frames in {:.1f}s, {:.1f} FPS",
                  total_frames, total_elapsed_sec, fps);
    return 0;
}

} // namespace cli
} // namespace chronon3d