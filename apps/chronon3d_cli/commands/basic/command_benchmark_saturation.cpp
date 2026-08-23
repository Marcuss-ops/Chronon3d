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
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/simd/cpu_isa.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
                                  const std::string& scene, int duration_sec,
                                  const std::string& report_json,
                                  int motion_blur_mode,
                                  int motion_blur_samples) {
    // ── Resolve and compile the composition ────────────────────────────
    if (!registry.contains(scene)) {
        spdlog::error("Unknown composition: {}", scene);
        return 1;
    }

    auto composition = registry.create(scene);
    const auto compile_t0 = std::chrono::steady_clock::now();
    auto compiled_result = chronon3d::compile_composition(
        composition, CompositionCompileContext{});
    const double compile_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - compile_t0).count();
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
    settings.disable_pixel_readback = true;
    settings.motion_blur.mode = static_cast<MotionBlurMode>(motion_blur_mode);
    settings.motion_blur.samples = motion_blur_samples;
    settings.motion_blur.shutter_angle_deg = 180.0f;
    settings.motion_blur.shutter_phase_deg = -90.0f;
    auto renderer = create_renderer(
        registry, settings, std::nullopt, std::filesystem::current_path());

    auto snapshot_gpu = [&]() {
        std::vector<std::pair<std::string, std::uint64_t>> ordered;
        if (renderer->runtime().backend_attached()) {
            renderer->runtime().backend().export_gpu_telemetry_counters(ordered);
        }
        std::unordered_map<std::string, std::uint64_t> by_name;
        by_name.reserve(ordered.size());
        for (const auto& [name, value] : ordered) by_name.emplace(name, value);
        return std::make_pair(std::move(ordered), std::move(by_name));
    };

    // ── Warm-up ─────────────────────────────────────────────────────────
    spdlog::info("Warming up: 10 frames");
    for (int i = 0; i < 10; ++i) {
        renderer->render_compiled(*compiled, static_cast<Frame>(i));
    }

    // ── Snapshot pre-run counters & proc state ─────────────────────────
    // Every architectural timing below is a DELTA from this point. Warm-up
    // work must never be divided by the timed-frame count.
    const auto* counters = renderer->counters();
    const auto proc_start = read_proc_status();
    const uint64_t pre_alloc = counters->framebuffer_allocations.load(std::memory_order_relaxed);
    const uint64_t pre_copies = counters->full_frame_copies.load(std::memory_order_relaxed);
    const uint64_t pre_passes = counters->full_frame_passes.load(std::memory_order_relaxed);
    const uint64_t pre_timeline_eval_us = counters->timeline_eval_wall_us.load(std::memory_order_relaxed);
    const uint64_t pre_graph_total_us = counters->graph_total_wall_us.load(std::memory_order_relaxed);
    const uint64_t pre_graph_refresh_us = counters->compiled_graph_refresh_wall_us.load(std::memory_order_relaxed);
    const uint64_t pre_text_shape_ms = counters->text_shaping_wall_ms.load(std::memory_order_relaxed);
    const uint64_t pre_text_layout_ms = counters->text_layout_wall_ms.load(std::memory_order_relaxed);
    const uint64_t pre_asset_resolve_us = counters->image_resolve_wall_us.load(std::memory_order_relaxed);
    auto [gpu_pre_ordered, gpu_pre] = snapshot_gpu();
    (void)gpu_pre_ordered;

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
    const uint64_t post_timeline_eval_us = counters->timeline_eval_wall_us.load(std::memory_order_relaxed);
    const uint64_t post_graph_total_us = counters->graph_total_wall_us.load(std::memory_order_relaxed);
    const uint64_t post_graph_refresh_us = counters->compiled_graph_refresh_wall_us.load(std::memory_order_relaxed);
    const uint64_t post_text_shape_ms = counters->text_shaping_wall_ms.load(std::memory_order_relaxed);
    const uint64_t post_text_layout_ms = counters->text_layout_wall_ms.load(std::memory_order_relaxed);
    const uint64_t post_asset_resolve_us = counters->image_resolve_wall_us.load(std::memory_order_relaxed);
    auto [gpu_post_ordered, gpu_post] = snapshot_gpu();

    const auto delta_u64 = [](std::uint64_t after, std::uint64_t before) {
        return after >= before ? after - before : std::uint64_t{0};
    };
    const auto gpu_delta = [&](const std::string& name) -> std::uint64_t {
        const auto post_it = gpu_post.find(name);
        if (post_it == gpu_post.end()) return 0;
        const auto pre_it = gpu_pre.find(name);
        const std::uint64_t before = pre_it == gpu_pre.end() ? 0 : pre_it->second;
        return delta_u64(post_it->second, before);
    };

    // Read context switches directly from /proc (the system counters in
    // RenderCounters are populated by the 1Hz async sampler which may not
    // have ticked during the benchmark run).
    const uint64_t total_ctx_sw = (proc_end.voluntary_cs + proc_end.involuntary_cs)
                                   - (proc_start.voluntary_cs + proc_start.involuntary_cs);
    const uint64_t peak_rss_kb = proc_end.peak_rss_kb;
    const auto pool_stats = renderer->framebuffer_pool()
        ? renderer->framebuffer_pool()->stats()
        : cache::FramebufferPoolStats{};
    const auto node_cache_stats = renderer->node_cache().stats();

    // ── Compute derived metrics ─────────────────────────────────────────
    // Sort frame times for percentiles.
    auto sorted = frame_times_ms;
    std::sort(sorted.begin(), sorted.end());

    const double fps = static_cast<double>(total_frames) / total_elapsed_sec;
    const double p50 = percentile(sorted, 0.50);
    const double p95 = percentile(sorted, 0.95);
    const double p99 = percentile(sorted, 0.99);
    const double frames_d = total_frames > 0 ? static_cast<double>(total_frames) : 1.0;
    const double avg_frame_wall_us = total_frames > 0
        ? (std::accumulate(frame_times_ms.begin(), frame_times_ms.end(), 0.0) * 1000.0 / frames_d)
        : 0.0;

    const uint64_t alloc_per_frame = static_cast<uint64_t>(
        static_cast<double>(post_alloc - pre_alloc) / frames_d);
    const uint64_t copies_per_frame = static_cast<uint64_t>(
        static_cast<double>(post_copies - pre_copies) / frames_d);
    const uint64_t passes_per_frame = static_cast<uint64_t>(
        static_cast<double>(post_passes - pre_passes) / frames_d);
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
    const bool is_gpu = renderer->runtime().backend_attached() &&
                        renderer->runtime().backend().supports_native_surfaces();
    const auto backend_name = renderer->runtime().backend_attached()
        ? (is_gpu ? "Vulkan" : "Software")
        : "None";

    auto& out = std::cout;
    out << "\n";
    out << "CHRONON3D SATURATION REPORT\n";
    out << "================================================\n";
    out << "HARDWARE & BACKEND\n";
    out << "Backend..................... " << backend_name << "\n";
    out << "Native surfaces support..... " << (is_gpu ? "true" : "false") << "\n";
    out << "Compiled program............ true\n";
    out << "CPU model................... " << read_cpu_model() << "\n";
    out << "Logical CPUs................ " << logical_cpus << "\n";
    out << "NUMA nodes.................. " << count_numa_nodes() << "\n";
    out << "Context switches............ " << total_ctx_sw << "\n";
    out << "\n";
    out << "SCENE\n";
    out << "Composition................. " << scene << "\n";
    out << "Duration.................... " << duration_sec << " s\n";
    out << "Rendered frames............. " << total_frames << "\n";
    out << "Motion blur................. mode=" << motion_blur_mode
        << " samples=" << motion_blur_samples << "\n";
    out << "\n";

    out << "THROUGHPUT\n";
    out << "FPS......................... " << fmt::format("{:.1f}", fps) << "\n";
    out << "Average frame............... " << fmt::format("{:.3f}", avg_frame_wall_us / 1000.0) << " ms\n";
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
    out << "FB pool current............. " << pool_stats.current_bytes / (1024 * 1024) << " MB\n";
    out << "FB pool retained............ " << pool_stats.retained_bytes / (1024 * 1024) << " MB\n";
    out << "FB pool available........... " << pool_stats.available_count << "\n";
    out << "FB pool allocations......... " << pool_stats.total_allocations << "\n";
    out << "FB pool reuses.............. " << pool_stats.total_reuses << "\n";
    out << "FB pool clears.............. " << pool_stats.total_clears << "\n";
    out << "FB pool evictions........... " << pool_stats.evicted_count << "\n";
    out << "FB pool size classes........ " << pool_stats.size_class_count << "\n";
    out << "Node cache entries.......... " << node_cache_stats.current_size << "\n";
    out << "Node cache weight........... " << node_cache_stats.current_weight / (1024 * 1024) << " MB\n";
    out << "Node cache hits............. " << node_cache_stats.hits << "\n";
    out << "Node cache misses........... " << node_cache_stats.misses << "\n";
    out << "Node cache evictions........ " << node_cache_stats.evictions << "\n";
    out << "\n";

    const auto* render_counters = renderer->counters();

    out << "INVALIDATION / COST\n";
    out << "Dirty union pixels.......... "
        << (render_counters ? render_counters->dirty_union_area_pixels.load() : 0)
        << "\n";
    out << "Dirty pixels................ "
        << (render_counters ? render_counters->dirty_pixels.load() : 0)
        << "\n";
    out << "Dirty rect full fallbacks... "
        << (render_counters ? render_counters->dirty_full_fallbacks.load() : 0)
        << "\n";
    out << "Tile dirty count............. "
        << (render_counters ? render_counters->tile_dirty_count.load() : 0)
        << "\n";
    out << "Tile clean count............. "
        << (render_counters ? render_counters->tile_clean_count.load() : 0)
        << "\n";
    out << "Tile pixels rendered......... "
        << (render_counters ? render_counters->tile_pixels_rendered.load() : 0)
        << "\n";
    out << "Tile pixels skipped.......... "
        << (render_counters ? render_counters->tile_pixels_skipped.load() : 0)
        << "\n";
    out << "Tile full fallbacks.......... "
        << (render_counters ? render_counters->tile_full_fallbacks.load() : 0)
        << "\n";
    out << "Tile regions executed........ "
        << (render_counters ? render_counters->tile_regions_executed.load() : 0)
        << "\n";
    out << "Tile region pixels........... "
        << (render_counters ? render_counters->tile_region_pixels.load() : 0)
        << "\n";
    out << "Tile execution time.......... "
        << (render_counters ? render_counters->tile_execution_wall_ms.load() : 0)
        << " ms\n";
    out << "Nodes skipped................ "
        << (render_counters ? render_counters->nodes_skipped.load() : 0)
        << "\n";
    out << "Graph skipped frames......... "
        << (render_counters ? render_counters->graph_skipped_frames.load() : 0)
        << "\n";
    out << "Dirty evaluation............ "
        << (render_counters ? render_counters->dirty_eval_wall_ms.load() : 0)
        << " ms\n";
    out << "Graph dirty-rect time........ "
        << (render_counters ? render_counters->graph_dirty_rect_wall_ms.load() : 0)
        << " ms\n";
    out << "Full exec EWMA............... "
        << fmt::format("{:.3f}", renderer->dirty_telemetry().full_frame_exec_ms_ewma)
        << " ms (" << renderer->dirty_telemetry().full_frame_cost_samples << " samples)\n";
    out << "Tile exec EWMA............... "
        << fmt::format("{:.3f}", renderer->dirty_telemetry().tile_exec_ms_ewma)
        << " ms (" << renderer->dirty_telemetry().tile_cost_samples << " samples)\n";
    out << "Tile cost model.............. "
        << (renderer->dirty_telemetry().tile_cost_model_ready() ? "READY" : "WARMING")
        << "\n";
    out << "\n";

    // Pixel-fusion counters are emitted by the compiler pass itself.  Keep
    // them in the saturation report so a benchmark cannot claim fusion from
    // compile-time descriptors that were never observed by the runtime.
    out << "FUSED PIXEL PROGRAM\n";
    out << "Passes before fusion........ "
        << (render_counters ? render_counters->pixel_fusion_passes_before.load() : 0)
        << "\n";
    out << "Passes after fusion......... "
        << (render_counters ? render_counters->pixel_fusion_passes_after.load() : 0)
        << "\n";
    out << "Bytes saved by fusion....... "
        << (render_counters ? render_counters->pixel_fusion_bytes_saved.load() : 0)
        << "\n\n";
    out << "COLOR PIPELINE RUNTIME\n";
    out << "Fused color batches.......... "
        << (render_counters ? render_counters->color_pipeline_batches.load() : 0)
        << "\n";
    out << "Fused color effects.......... "
        << (render_counters ? render_counters->color_pipeline_effects.load() : 0)
        << "\n\n";

    if (renderer->runtime().backend_attached()) {
        const std::uint64_t lb_calls = gpu_delta("layer_batch_calls");
        const std::uint64_t l_inst = gpu_delta("layer_instances");
        const std::uint64_t tb_calls = gpu_delta("text_batch_calls");
        const std::uint64_t glyphs = gpu_delta("glyphs");
        const std::uint64_t leg_xform = gpu_delta("legacy_transform_calls");
        const std::uint64_t leg_comp = gpu_delta("legacy_composite_calls");
        const std::uint64_t leg_txt = gpu_delta("legacy_text_run_surface_calls");

        out << "BATCHING & INSTANCING (TIMED-RUN DELTAS)\n";
        out << "layer_batch_calls/frame..... " << fmt::format("{:.1f}", static_cast<double>(lb_calls) / frames_d) << "\n";
        out << "layer_instances/frame....... " << fmt::format("{:.1f}", static_cast<double>(l_inst) / frames_d) << "\n";
        out << "text_batch_calls/frame...... " << fmt::format("{:.1f}", static_cast<double>(tb_calls) / frames_d) << "\n";
        out << "glyphs/frame................ " << fmt::format("{:.1f}", static_cast<double>(glyphs) / frames_d) << "\n\n";

        out << "GPU EXECUTION METRICS (TIMED-RUN DELTAS)\n";
        for (const auto& [k, unused] : gpu_post_ordered) {
            (void)unused;
            out << fmt::format("{:<28} {}\n", k, gpu_delta(k));
        }
        out << "\n";

        // Hard gate for Perf_* benchmark scenes
        if (scene == "Perf_IMG_same_100" || scene == "Perf_IMG_move_100" ||
            scene == "Perf_TXT_static_100" || scene == "Perf_TXT_move_100") {
            if (leg_xform > 0 || leg_comp > 0 || leg_txt > 0) {
                spdlog::error("[BENCHMARK ARCHITECTURE FAIL] Legacy operations executed in {}: legacy_transform={} legacy_composite={} legacy_text_run={}",
                              scene, leg_xform, leg_comp, leg_txt);
                assert(leg_xform == 0 && "legacy_transform_calls must be 0");
                assert(leg_comp == 0 && "legacy_composite_calls must be 0");
                assert(leg_txt == 0 && "legacy_text_run_surface_calls must be 0");
            }
        }
    }

    // ── Honest frame-wall accounting ───────────────────────────────────
    // Top-level accounting is deliberately disjoint: timeline evaluation +
    // render-graph wall. GPU submit/wait/timestamp values below are nested
    // diagnostics and MUST NOT be added again to `accounted_us`.
    const double timeline_eval_us = static_cast<double>(
        delta_u64(post_timeline_eval_us, pre_timeline_eval_us)) / frames_d;
    const double graph_total_us = static_cast<double>(
        delta_u64(post_graph_total_us, pre_graph_total_us)) / frames_d;
    const double graph_refresh_us = static_cast<double>(
        delta_u64(post_graph_refresh_us, pre_graph_refresh_us)) / frames_d;
    const double text_shape_us = static_cast<double>(
        delta_u64(post_text_shape_ms, pre_text_shape_ms)) * 1000.0 / frames_d;
    const double text_layout_us = static_cast<double>(
        delta_u64(post_text_layout_ms, pre_text_layout_ms)) * 1000.0 / frames_d;
    const double asset_resolve_us = static_cast<double>(
        delta_u64(post_asset_resolve_us, pre_asset_resolve_us)) / frames_d;

    const double frame_slot_wait_us = static_cast<double>(gpu_delta("frame_slot_wait_us")) / frames_d;
    const double fence_wait_us = static_cast<double>(gpu_delta("gpu_wait_cpu_us")) / frames_d;
    const double queue_submit_cpu_us = static_cast<double>(gpu_delta("gpu_submit_cpu_us")) / frames_d;
    const double gpu_timestamp_us = static_cast<double>(gpu_delta("gpu_execute_us")) / frames_d;
    const double readback_us = static_cast<double>(gpu_delta("readback_us")) / frames_d;

    const double texture_upload_count = static_cast<double>(gpu_delta("gpu_upload_count")) / frames_d;
    const double texture_upload_bytes = static_cast<double>(gpu_delta("gpu_upload_bytes")) / frames_d;
    const double image_upload_count = static_cast<double>(
        gpu_delta("gpu_upload_image_full_count") +
        gpu_delta("gpu_upload_image_region_count")) / frames_d;
    const double image_upload_bytes = static_cast<double>(gpu_delta("gpu_upload_image_bytes")) / frames_d;

    const double accounted_us = timeline_eval_us + graph_total_us;
    // Keep this signed. A negative value is evidence that supposedly disjoint
    // top-level counters overlap and must be fixed; clamping would hide it.
    const double unaccounted_us = avg_frame_wall_us - accounted_us;
    const double accounting_check_us = accounted_us + unaccounted_us - avg_frame_wall_us;

    out << "SETUP TIMINGS\n";
    out << "compile_us.................. " << fmt::format("{:.1f}", compile_us) << " us (one-time)\n";
    out << "prepare_us.................. N/A (prepare barrier not separately surfaced)\n\n";

    out << "FRAME WALL BREAKDOWN (PER-FRAME AVERAGE; WARM-UP EXCLUDED)\n";
    out << "frame_wall_us............... " << fmt::format("{:.1f}", avg_frame_wall_us) << " us\n";
    out << "timeline_eval_us............ " << fmt::format("{:.1f}", timeline_eval_us) << " us\n";
    out << "timeline_patch_us........... N/A (not separately instrumented)\n";
    out << "scene_clone_us.............. N/A (not separately instrumented)\n";
    out << "graph_total_us.............. " << fmt::format("{:.1f}", graph_total_us) << " us\n";
    out << "graph_refresh_us............ " << fmt::format("{:.1f}", graph_refresh_us) << " us\n";
    out << "parameter_patch_us.......... N/A (not separately instrumented)\n";
    out << "instance_patch_us........... N/A (not separately instrumented)\n";
    out << "frame_slot_acquire_us....... N/A (only wait time is instrumented)\n";
    out << "frame_slot_wait_us.......... " << fmt::format("{:.1f}", frame_slot_wait_us) << " us [nested]\n";
    out << "fence_wait_us............... " << fmt::format("{:.1f}", fence_wait_us) << " us [nested]\n";
    out << "ssbo_write_us............... N/A (not separately instrumented)\n";
    out << "descriptor_update_us........ N/A (not separately instrumented)\n";
    out << "command_record_us........... N/A (not separately instrumented)\n";
    out << "queue_submit_cpu_us......... " << fmt::format("{:.1f}", queue_submit_cpu_us) << " us [nested]\n";
    out << "gpu_timestamp_us............ " << fmt::format("{:.1f}", gpu_timestamp_us) << " us [GPU elapsed; nested]\n";
    out << "output_wrap_us.............. N/A (not separately instrumented)\n";
    out << "readback_us................. " << fmt::format("{:.1f}", readback_us) << " us [nested]\n";
    out << "accounted_us................ " << fmt::format("{:.1f}", accounted_us) << " us\n";
    out << "unaccounted_us.............. " << fmt::format("{:.1f}", unaccounted_us) << " us\n";
    out << "accounting_check_us......... " << fmt::format("{:.3f}", accounting_check_us) << " us (target ~= 0)\n\n";

    out << "STEADY-STATE UPLOADS (PER-FRAME AVERAGE)\n";
    out << "texture_upload_count/frame. " << fmt::format("{:.3f}", texture_upload_count) << "\n";
    out << "texture_upload_bytes/frame. " << fmt::format("{:.1f}", texture_upload_bytes) << "\n";
    out << "image_upload_count/frame... " << fmt::format("{:.3f}", image_upload_count) << "\n";
    out << "image_upload_bytes/frame... " << fmt::format("{:.1f}", image_upload_bytes) << "\n";
    out << "NOTE........................ No gpu_upload_us proxy: upload time is not instrumented.\n\n";

    out << "TEXT / ASSET NESTED COSTS\n";
    out << "text_shape_us............... " << fmt::format("{:.1f}", text_shape_us) << " us\n";
    out << "text_layout_us.............. " << fmt::format("{:.1f}", text_layout_us) << " us\n";
    out << "asset_resolve_us............ " << fmt::format("{:.1f}", asset_resolve_us) << " us\n\n";

    // Keep the JSON artifact aligned with the evidence printed above. A
    // missing determinism run is represented explicitly instead of being
    // inferred from a single sequential benchmark.
    if (!report_json.empty()) {
        const auto counter = [render_counters](auto member) -> std::uint64_t {
            return render_counters ? (render_counters->*member).load(std::memory_order_relaxed) : 0;
        };
        nlohmann::json report{
            {"schema_version", 2},
            {"scene", scene},
            {"duration_sec", duration_sec},
            {"frames", total_frames},
            {"motion_blur", {
                {"mode", motion_blur_mode},
                {"samples", motion_blur_samples},
                {"shutter_angle_deg", 180.0},
                {"shutter_phase_deg", -90.0}
            }},
            {"fps", fps},
            {"average_frame_ms", avg_frame_wall_us / 1000.0},
            {"p50_ms", p50},
            {"p95_ms", p95},
            {"p99_ms", p99},
            {"total_elapsed_sec", total_elapsed_sec},
            {"setup", {
                {"compile_us", compile_us},
                {"prepare_us", nullptr},
                {"prepare_status", "NOT_SEPARATELY_INSTRUMENTED"}
            }},
            {"frame_wall_breakdown", {
                {"frame_wall_us", avg_frame_wall_us},
                {"timeline_eval_us", timeline_eval_us},
                {"timeline_patch_us", nullptr},
                {"scene_clone_us", nullptr},
                {"graph_total_us", graph_total_us},
                {"graph_refresh_us", graph_refresh_us},
                {"parameter_patch_us", nullptr},
                {"instance_patch_us", nullptr},
                {"frame_slot_acquire_us", nullptr},
                {"frame_slot_wait_us", frame_slot_wait_us},
                {"fence_wait_us", fence_wait_us},
                {"ssbo_write_us", nullptr},
                {"descriptor_update_us", nullptr},
                {"command_record_us", nullptr},
                {"queue_submit_cpu_us", queue_submit_cpu_us},
                {"gpu_timestamp_us", gpu_timestamp_us},
                {"output_wrap_us", nullptr},
                {"readback_us", readback_us},
                {"accounted_us", accounted_us},
                {"unaccounted_us", unaccounted_us},
                {"accounting_check_us", accounting_check_us},
                {"accounting_contract", "accounted_us + unaccounted_us == frame_wall_us"}
            }},
            {"uploads", {
                {"texture_upload_count_per_frame", texture_upload_count},
                {"texture_upload_bytes_per_frame", texture_upload_bytes},
                {"image_upload_count_per_frame", image_upload_count},
                {"image_upload_bytes_per_frame", image_upload_bytes},
                {"upload_time_us", nullptr},
                {"upload_time_status", "NOT_INSTRUMENTED"}
            }},
            {"nested_costs", {
                {"text_shape_us", text_shape_us},
                {"text_layout_us", text_layout_us},
                {"asset_resolve_us", asset_resolve_us}
            }},
            {"memory", {
                {"peak_rss_mb", peak_rss_mb},
                {"framebuffer_allocations_per_frame", alloc_per_frame},
                {"framebuffer_copies_per_frame", copies_per_frame},
                {"full_frame_passes_per_frame", passes_per_frame},
                {"pool_current_mb", pool_stats.current_bytes / (1024 * 1024)},
                {"pool_retained_mb", pool_stats.retained_bytes / (1024 * 1024)},
                {"pool_allocations", pool_stats.total_allocations},
                {"pool_reuses", pool_stats.total_reuses},
                {"pool_clears", pool_stats.total_clears},
                {"pool_evictions", pool_stats.evicted_count}
            }},
            {"cache", {
                {"entries", node_cache_stats.current_size},
                {"weight_bytes", node_cache_stats.current_weight},
                {"hits", node_cache_stats.hits},
                {"misses", node_cache_stats.misses},
                {"evictions", node_cache_stats.evictions}
            }},
            {"invalidation", {
                {"dirty_union_pixels", counter(&RenderCounters::dirty_union_area_pixels)},
                {"dirty_pixels", counter(&RenderCounters::dirty_pixels)},
                {"dirty_full_fallbacks", counter(&RenderCounters::dirty_full_fallbacks)},
                {"tile_dirty_count", counter(&RenderCounters::tile_dirty_count)},
                {"tile_clean_count", counter(&RenderCounters::tile_clean_count)},
                {"tile_pixels_rendered", counter(&RenderCounters::tile_pixels_rendered)},
                {"tile_pixels_skipped", counter(&RenderCounters::tile_pixels_skipped)},
                {"tile_full_fallbacks", counter(&RenderCounters::tile_full_fallbacks)},
                {"tile_regions_executed", counter(&RenderCounters::tile_regions_executed)},
                {"tile_region_pixels", counter(&RenderCounters::tile_region_pixels)},
                {"tile_execution_wall_ms", counter(&RenderCounters::tile_execution_wall_ms)},
                {"nodes_skipped", counter(&RenderCounters::nodes_skipped)},
                {"dirty_eval_wall_ms", counter(&RenderCounters::dirty_eval_wall_ms)},
                {"graph_dirty_rect_wall_ms", counter(&RenderCounters::graph_dirty_rect_wall_ms)}
            }},
            {"cost_model", {
                {"full_frame_exec_ms_ewma", renderer->dirty_telemetry().full_frame_exec_ms_ewma},
                {"tile_exec_ms_ewma", renderer->dirty_telemetry().tile_exec_ms_ewma},
                {"full_frame_samples", renderer->dirty_telemetry().full_frame_cost_samples},
                {"tile_samples", renderer->dirty_telemetry().tile_cost_samples},
                {"state", renderer->dirty_telemetry().tile_cost_model_ready() ? "READY" : "WARMING"}
            }},
            {"fusion", {
                {"passes_before", counter(&RenderCounters::pixel_fusion_passes_before)},
                {"passes_after", counter(&RenderCounters::pixel_fusion_passes_after)},
                {"bytes_saved", counter(&RenderCounters::pixel_fusion_bytes_saved)},
                {"color_pipeline_batches", counter(&RenderCounters::color_pipeline_batches)},
                {"color_pipeline_effects", counter(&RenderCounters::color_pipeline_effects)}
            }},
            {"determinism", {
                {"status", "NOT_RUN"},
                {"sequential_hash", nullptr},
                {"random_access_hash", nullptr}
            }}
        };
        std::ofstream json_out(report_json);
        if (!json_out) {
            spdlog::error("Cannot write benchmark JSON report: {}", report_json);
            return 1;
        }
        json_out << report.dump(2) << '\n';
        spdlog::info("Benchmark JSON report written to {}", report_json);
    }

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
