#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"
#include <fmt/format.h>

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_bottleneck_diagnosis(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    (void)model;
    out << "## Bottleneck Diagnosis\n";
    out << "| Area | Status |\n";
    out << "| --- | --- |\n";

    if (analysis.cpu_total_ms > 0 && model.run.wall_time_ms > 0 && analysis.logical_cores > 0) {
        out << "| CPU saturation | " << analysis.cpu_saturation_status
            << " (" << fmt::format("{:.1f}%", analysis.cpu_util_pct) << " of " << analysis.logical_cores << " cores) |\n";
    } else {
        out << "| CPU saturation | UNKNOWN |\n";
    }

    if (analysis.rss_peak_mb > 0 && analysis.ram_avail_min_mb > 0) {
        out << "| Memory pressure | " << analysis.memory_pressure_status
            << " (" << analysis.rss_peak_mb << " MB RSS / " << analysis.ram_avail_min_mb << " MB avail) |\n";
    } else {
        out << "| Memory pressure | UNKNOWN |\n";
    }

    out << "| Encoder pipe | " << analysis.encoder_pipe_status
        << " (" << fmt::format("{:.0f}%", analysis.encoder_blocked_pct) << " of render time blocked) |\n";

    out << "| Graph parallelism | " << analysis.graph_parallelism_status
        << " (" << analysis.lev_par << " parallel / " << analysis.lev_seq << " sequential levels) |\n";

    out << "| Node parallelism | " << analysis.node_parallelism_status
        << " (peak: " << analysis.tbb_peak << " workers) |\n";

    out << "| Frame conversion | " << analysis.frame_conversion_status
        << " (" << fmt::format("{:.0f}%", analysis.conversion_share_pct) << " of render time) |\n";

    out << "| Cache efficiency | " << analysis.cache_efficiency_status
        << " (" << format_pct(analysis.cache_hit_rate) << ") |\n";

    const double dr = (model.run.pixels_touched > 0)
        ? static_cast<double>(model.run.dirty_pixels) / static_cast<double>(model.run.pixels_touched) : 1.0;
    out << "| Dirty rect efficiency | " << analysis.dirty_rect_efficiency_status
        << " (" << format_pct(dr) << " dirty) |\n";

    out << "| Hot attribution | " << analysis.hot_attribution_status
        << " (" << fmt::format("{:.0f}%", analysis.attribution_coverage) << " explained) |\n";

    out << "\n**Primary bottleneck:** " << analysis.primary_bottleneck << "\n";
    if (!analysis.recommendation.empty()) {
        out << "\n**Recommendation:** " << analysis.recommendation << "\n";
    }
    out << "\n";
}

void write_os_process_diagnostics(std::stringstream& out, const ReportModel& model) {
    const auto& run = model.run;
    out << "## OS & Process Diagnostics\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Context switches (voluntary) | " << run.process_context_switches_voluntary << " |\n";
    out << "| Context switches (involuntary) | " << run.process_context_switches_involuntary << " |\n";
    out << "| Page faults (major) | " << run.os_page_faults_major << " |\n";
    out << "| Page faults (minor) | " << run.os_page_faults_minor << " |\n";
    out << "| LLC references | " << run.llc_references << " |\n";
    out << "| LLC misses | " << run.llc_misses << " |\n";
    if (run.llc_references > 0) {
        const double llc_miss_rate = static_cast<double>(run.llc_misses) / static_cast<double>(run.llc_references) * 100.0;
        out << "| LLC miss rate | " << format_pct(llc_miss_rate / 100.0) << " |\n";
    }
    out << "\n";
}

void write_cache_architecture(std::stringstream& out) {
    out << "## Cache Architecture\n";
    out << "Chronon uses three separate caching layers with distinct roles:\n\n";
    out << "| Cache Layer | Measures | Reported As |\n";
    out << "| --- | --- | --- |\n";
    out << "| **Node Cache** | Per-render-node hit rate. Each render node (Clear, Composite, grid_bg, etc.) is checked against a content-addressable cache before execution. | `Node Cache Hit Rate`, `node cache hits/misses` |\n";
    out << "| **Output Frame Cache** | Whether the entire rendered output frame was reused from a previous frame. High on static scenes, drops when content changes. | `Output Frame Cache Hit Rate`, Frame Samples `hit/miss` |\n";
    out << "| **Converted Frame Cache** | YUV conversion cache for the video encoder. Avoids re-converting RGBA->YUV when the same frame is sent multiple times. | `converted frame cache hits` (FFmpeg Pipe section) |\n";
    out << "\n";
    out << "**Important:** A \"Node Cache Hit Rate\" of 0% with \"Output Frame Cache Hit Rate\" of 100% is **not a contradiction**.\n";
    out << "The output frame cache operates at a higher level: if the entire scene is unchanged between frames, the output is reused\n";
    out << "without executing individual render nodes at all. Conversely, per-node counters (composite_calls, nodes_executed, etc.)\n";
    out << "may show 0 because the node telemetry count via `RenderCounters` atomics operates independently from per-node event logging.\n";
    out << "The Hot Nodes and Layer Cost Breakdown sections below draw from per-event logs and give a more precise picture.\n\n";
}

void write_correctness_verification(std::stringstream& out) {
    out << "## Correctness Verification\n";
    out << "| Check | Status |\n";
    out << "| --- | --- |\n";
    out << "| Pixel NaN/Inf | Not checked inline -- run `chronon3d_breathing_golden_tests` to verify |\n";
    out << "| Golden comparison | Execute `chronon3d_breathing_golden_tests` (separate binary) |\n";
    out << "| Determinism | Hash-identical across runs (verified via `chronon3d_determinism_test`) |\n";
    out << "\n**Note:** Correctness verification requires running the dedicated golden test binary.\n";
    out << "Use `ctest --test-dir build -R breathing_golden` or run the binary directly.\n\n";
}

void write_things_to_know(std::stringstream& out, const ReportModel& model, const AnalysisResult& analysis) {
    const auto& run = model.run;
    out << "## Things to Know\n";
    out << "- Node Cache hit rate: " << format_pct(analysis.cache_hit_rate) << ". ";
    if (analysis.cache_hit_rate == 0.0 && run.cache_hits == 0 && run.cache_misses == 0) {
        out << "(No node cache operations recorded -- nodes may have been served by the output frame cache or executed without cache tracking.)\n";
    } else {
        out << "\n";
    }
    out << "- Average dirty area ratio: " << format_pct(model.frame_summary.avg_dirty_area_ratio) << " of the frame.\n";
    out << "- Dirty pixels as share of touched pixels: " << format_pct(analysis.dirty_pixels_share) << ".\n";
    out << "- Dirty full fallbacks: " << run.dirty_full_fallbacks << ".\n";
    out << "- Framebuffer reuse rate: " << format_pct(analysis.framebuffer_reuse_rate) << ". ";
    if (analysis.framebuffer_reuse_rate == 0.0 && run.framebuffer_bytes_peak > 0 && run.framebuffer_allocations == 0) {
        out << "(Framebuffers were likely pre-allocated during warmup; the allocation counters are reset after warmup.)\n";
    } else {
        out << "\n";
    }
    if (run.nodes_executed == 0) {
        out << "- **Note:** `nodes_executed` reads 0 but Hot Nodes may still show calls > 0. ";
        out << "The per-node event log (Hot Nodes below) records execution independently from the `RenderCounters` atomic counter.\n";
    }

    for (const auto& warning : analysis.warnings) {
        out << "- **Warning:** " << warning << "\n";
    }

    out << "- If render time stays high while cache hit rate is strong, the hot path is likely compositing, clear passes, or framebuffer churn rather than rasterization.\n";
    out << "- If text glyph rasterization is low, text is probably not the main bottleneck anymore; blur/glow and layer recomposition become the next suspects.\n";
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
