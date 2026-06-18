#include "command_telemetry_internal.hpp"

#include <fmt/format.h>
#include <sstream>
#include <string>
#include <cstdint>

namespace chronon3d::cli {

std::string format_pct(double value) {
    return fmt::format("{:.1f}%", value * 100.0);
}

std::string format_bytes(uint64_t bytes) {
    return fmt::format("{:.2f} GB", static_cast<double>(bytes) / 1'000'000'000.0);
}

std::string format_ms(double value) {
    return fmt::format("{:.2f} ms", value);
}

} // namespace chronon3d::cli

#include <sqlite3.h>

namespace chronon3d::cli {

std::string sql_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? reinterpret_cast<const char*>(txt) : "";
}

int64_t sql_i64(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int64(stmt, col);
}

double sql_double(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_double(stmt, col);
}

bool prepare_with_run_id(sqlite3* db, sqlite3_stmt** stmt, const char* sql, const std::string& run_id) {
    if (sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(*stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    return true;
}

// Minimal stub: emit a Markdown summary from `run` only.
// `query_run_summary` already aggregates everything needed; this stub intentionally
// avoids re-querying SQLite so it works even when the upstream implementation is
// missing. The reference is to `RunSummary` fields and helpers already provided by
// this translation unit.
void generate_telemetry_report(std::stringstream& out, sqlite3* db, const std::string& run_id, const RunSummary& run) {
    (void)db; // intentionally unused in this stub
    out << "# Telemetry Report\n\n";
    out << "- **Run ID:** `" << run.run_id << "`\n";
    out << "- **Composition:** `" << run.composition_id << "`\n";
    out << "- **Output path:** `" << run.output_path << "`\n";
    out << "- **Success:** " << (run.success ? "yes" : "no") << "\n";
    if (!run.error_code.empty() || !run.error_message.empty()) {
        out << "- **Error:** " << run.error_code << " — " << run.error_message << "\n";
    }
    out << "- **Started:** " << run.started_at_iso << "\n";
    out << "- **Finished:** " << run.finished_at_iso << "\n";
    out << "- **Host:** " << run.os << " / " << run.compiler_info
        << " / cores=" << run.cores << " / build=" << run.build_type
        << " / git=" << run.git_commit_short << "\n\n";

    out << "## Frame timings\n";
    out << "- Frames total / written: " << run.frames_total << " / " << run.frames_written << "\n";
    out << "- Wall time: " << format_ms(run.wall_time_ms) << "\n";
    out << "- Render time: " << format_ms(run.render_ms) << "\n";
    out << "- Encode time: " << format_ms(run.encode_ms) << "\n";
    out << "- Effective FPS: " << run.effective_fps << "\n\n";

    out << "## Throughput (Chronon render pipeline)\n";
    out << "- Chronon render-only: " << format_ms(run.chronon_render_only_ms) << "\n";
    out << "- Chronon conversion+copy: " << format_ms(run.chronon_conversion_copy_ms) << "\n";
    out << "- Chronon queue wait: " << format_ms(run.chronon_queue_wait_ms) << "\n";
    out << "- Chronon throughput (total): " << format_ms(run.chronon_render_throughput_ms) << "\n";
    out << "- FFmpeg encode total: " << format_ms(run.ffmpeg_encode_total_ms) << "\n";
    out << "- FFmpeg flush+close: " << format_ms(run.ffmpeg_flush_close_ms) << "\n";
    out << "- End-to-end wall: " << format_ms(run.e2e_wall_ms) << "\n\n";

    out << "## Counters\n";
    out << "- Pixels touched: " << run.pixels_touched << "\n";
    out << "- Cache hits / misses: " << run.cache_hits << " / " << run.cache_misses << "\n";
    out << "- Nodes executed: " << run.nodes_executed << "\n";
    out << "- Layers rendered: " << run.layers_rendered << "\n";
    out << "- Layers culled / visible: " << run.layers_culled << " / " << run.layers_visible << "\n";
    out << "- Composite calls / pixels: " << run.composite_calls << " / " << run.composite_pixels << "\n";
    out << "- Transform calls / pixels: " << run.transform_calls << " / " << run.transform_pixels << "\n";
    out << "- Effect stack calls / pixels: " << run.effect_stack_calls << " / " << run.effect_pixels << "\n";
    out << "- Blur pixels: " << run.blur_pixels << "\n";
    out << "- SIMD lerp calls: " << run.simd_lerp_calls << "\n";
    out << "- Text glyphs rasterized: " << run.text_glyphs_rasterized << "\n";
    out << "- Images sampled: " << run.images_sampled << "\n\n";

    out << "## Dirty rects\n";
    out << "- Dirty rects: " << run.dirty_rect_count << " (" << run.dirty_pixels << " px)\n";
    out << "- Union area pixels: " << run.dirty_union_area_pixels << "\n";
    out << "- Full fallbacks: " << run.dirty_full_fallbacks
        << " / predicted_missing=" << run.dirty_full_fallback_predicted_bounds_missing
        << ", composite_missing=" << run.dirty_full_fallback_composite_missing_input_bounds
        << ", transform_unknown=" << run.dirty_full_fallback_transform_bounds_unknown
        << ", effect_unknown=" << run.dirty_full_fallback_effect_bounds_unknown << "\n\n";

    out << "## Memory\n";
    out << "- Framebuffer allocations: " << run.framebuffer_allocations
        << " (reuses: " << run.framebuffer_reuses << ")\n";
    out << "- Framebuffer bytes allocated / peak: "
        << run.framebuffer_bytes_allocated << " / " << run.framebuffer_bytes_peak << "\n";
    out << "- Tiles hit / miss / partial: " << run.tiles_hit << " / " << run.tiles_miss << " / " << run.tiles_partial << "\n";
    out << "- Peak memory: " << run.bytes_allocated_peak << " bytes\n\n";

    (void)run_id; // referenced for future expansions
}

} // namespace chronon3d::cli
