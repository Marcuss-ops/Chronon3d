#include "frame_state_commit.hpp"
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/config.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <filesystem>
#include <fstream>
#include <cmath>

namespace chronon3d::graph {

void record_frame_timings(
    RenderCounters* counters,
    const FrameTimings& timings)
{
    if (!counters) return;

    // All counters are cumulative per-frame — use fetch_add to accumulate
    // across consecutive frames in a multi-frame render or benchmark.
    auto add_ms = [&](std::atomic<uint64_t>& counter, double ms) {
        counter.fetch_add(static_cast<uint64_t>(std::llround(std::max(0.0, ms))),
            std::memory_order_relaxed);
    };

    add_ms(counters->graph_resolve_layers_ms, timings.resolve_ms);
    add_ms(counters->graph_dirty_rect_ms, timings.dirty_ms);
    add_ms(counters->graph_build_ms, timings.graph_ms);
    add_ms(counters->graph_execute_ms, timings.exec_ms);
    add_ms(counters->graph_total_ms, timings.total_graph_ms);
}

void setup_pingpong_buffers(
    SoftwareRenderer* sw_renderer,
    int width,
    int height)
{
    if (!sw_renderer) return;

    if (sw_renderer->config().scheduler().pingpong_framebuffer()) {
        sw_renderer->buffer_ring().ensure_size(width, height,
            sw_renderer->framebuffer_pool().get());
    }
}

void commit_frame_state(
    SoftwareRenderer* sw_renderer,
    Frame frame,
    const Camera2_5D& camera,
    CompiledFrameGraph&& compiled,
    std::shared_ptr<Framebuffer>& fb_shared,
    const detail::LayerResolutionResult& resolved,
    const FrameFingerprints& fingerprints,
    detail::DirtyRectOutput& dirty_out,
    int width,
    int height)
{
    if (!sw_renderer) return;

    // Cache render graph for incremental reuse next frame
    sw_renderer->graph_cache().store(std::move(compiled), width, height);

    // ── Update prev_framebuffer: swap ping-pong or assign directly ──
    if (fb_shared.get() == sw_renderer->buffer_ring().ping_fb(0) ||
        fb_shared.get() == sw_renderer->buffer_ring().ping_fb(1))
    {
        // Rendered into a ping — swap indices to advance the ping cycle.
        sw_renderer->buffer_ring().swap();
    } else {
        // Rendered into a pool FB — assign directly.
        sw_renderer->buffer_ring().prev_framebuffer() = fb_shared;
    }

    sw_renderer->commit_prev_frame_state(
        frame,
        resolved.camera.camera,
        fingerprints.combined_fp,
        fingerprints.static_fp,
        fingerprints.structure_fp,
        fingerprints.active_at_fp,
        std::move(dirty_out.layer_bboxes));
}

// ── Output-path helpers ─────────────────────────────────────────────────

std::string format_plan_output_path(std::string pattern, Frame frame) {
    const std::string replacement = fmt::format("{:04d}", static_cast<int64_t>(frame));
    const auto pos = pattern.find("####");
    if (pos != std::string::npos) {
        pattern.replace(pos, 4, replacement);
        return pattern;
    }

    const auto dot_pos = pattern.find_last_of('.');
    if (dot_pos != std::string::npos) {
        pattern.insert(dot_pos, "_" + replacement);
    } else {
        pattern += "_" + replacement;
    }
    return pattern;
}

bool write_plan_output_file(const std::string& path, const std::string& contents) {
    if (path.empty()) return true;

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            spdlog::error("[graph-preflight] cannot create directory '{}': {}",
                p.parent_path().string(), ec.message());
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        spdlog::error("[graph-preflight] cannot open '{}' for writing", path);
        return false;
    }
    out << contents;
    if (!out) {
        spdlog::error("[graph-preflight] failed while writing '{}'", path);
        return false;
    }
    return true;
}

} // namespace chronon3d::graph
