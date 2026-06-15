// ---------------------------------------------------------------------------
// null_render_exporter.cpp — NullRenderExporter
//
/// Benchmark-only sink: renders every frame but discards the output.
///
/// Measures raw render throughput without the overhead of encoding, pipe
/// I/O, or PNG writes.  Useful for:
///   - Isolating render pipeline performance from I/O bottlenecks
///   - CI benchmarks where output artifacts aren't needed
///   - Profiling render-phase CPU/GPU usage
// ---------------------------------------------------------------------------

#include "../exporter_registry.hpp"
#include "../common/video_export_common.hpp"
#include "../common/output_options.hpp"
#include "../common/encoder_options.hpp"
#include "../common/warmup_options.hpp"
#include "../../../utils/job/cli_render_utils.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {
namespace {

class NullRenderExporter final : public VideoExporter {
public:
    std::string_view id() const override { return "null-render"; }
    std::string_view description() const override {
        return "Render frames to memory then discard (benchmark render path only)";
    }

    int export_video(const VideoExportJob& job) override {
        const Frame start = job.start;
        const Frame end   = job.end;
        const int total   = static_cast<int>(end - start);
        const auto& opts  = job.opts;

        spdlog::info("[null-render] Rendering {} frames [{}, {}) at {} fps — no output written",
                     total, start, end, opts.output.fps);

        profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
        profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

        const auto wall_t0 = profiling::now();

        // ── Create renderer ─────────────────────────────────────────────
        auto renderer = create_renderer(job.registry, job.settings);
        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
        if (!sw_renderer) {
            spdlog::error("[null-render] Requires SoftwareRenderer backend");
            return 1;
        }

        // ── Warmup (optional) ───────────────────────────────────────────
        if (opts.warmup.warmup_renderer) {
            const auto warmup_t0 = profiling::now();
            runtime::warmup_renderer(*sw_renderer, job.comp, runtime::RendererWarmupOptions{
                .width = job.comp.width(),
                .height = job.comp.height(),
                .framebuffer_count = opts.warmup.warmup_framebuffers,
                .preallocate_framebuffers = true,
                .touch_memory = true,
                .render_dummy_frame = opts.warmup.warmup_dummy_frame,
                .dummy_frame = 0,
                .quiet = false,
            });
            const auto warmup_t1 = profiling::now();
            spdlog::info("[null-render] Warmup: {:.1f} ms",
                         profiling::duration_ms(warmup_t0, warmup_t1));
        }

        // ── Render loop (no output) ─────────────────────────────────────
        int frames_done = 0;
        for (Frame f = start; f < end; ++f) {
            const auto t0 = profiling::now();
            auto fb = sw_renderer->render_frame(job.comp, f);
            const auto t1 = profiling::now();

            if (!fb) {
                spdlog::error("[null-render] Render failed at frame {}", f);
                return 1;
            }

            ++frames_done;
            if (frames_done % std::max(1, total / 10) == 0 || frames_done == total) {
                spdlog::info("[null-render]   {}/{} frames ({:.1f} ms/frame)",
                             frames_done, total,
                             profiling::duration_ms(t0, t1));
            }
        }

        const auto wall_t1 = profiling::now();
        const double wall_ms = profiling::duration_ms(wall_t0, wall_t1);

        spdlog::info("[null-render] ✅ {} frames in {:.1f} ms ({:.1f} fps)",
                     frames_done, wall_ms,
                     frames_done / (wall_ms / 1000.0));

        return 0;
    }
};

} // namespace

void register_null_render_sink(ExporterRegistry& registry) {
    registry.register_exporter(std::make_unique<NullRenderExporter>());
}

} // namespace chronon3d::cli
