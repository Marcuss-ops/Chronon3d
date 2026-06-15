// ---------------------------------------------------------------------------
// null_convert_exporter.cpp — NullConvertExporter
//
/// Benchmark-only sink: renders every frame, applies colour conversion and
/// pixel format conversion (RGBA → YUV), then discards the result.
///
/// Measures the full render + convert pipeline throughput without the I/O
/// overhead of pipe writes or file I/O.  Useful for:
///   - Isolating colour conversion performance
///   - Comparing native vs pipe encoder throughput (without I/O)
// ---------------------------------------------------------------------------

#include "../exporter_registry.hpp"
#include "../common/video_export_common.hpp"
#include "../../../utils/job/cli_render_utils.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <spdlog/spdlog.h>

#include <vector>
#include <cstdint>

namespace chronon3d::cli {
namespace {

/// Simple RGBA → YUV420p conversion (dummy conversion for benchmarking).
/// This is intentionally not optimised — the null-convert sink exists to
/// measure the cost of doing *any* conversion, not to be a fast path.
[[nodiscard]] std::vector<uint8_t> convert_rgba_to_yuv420p(
    const Framebuffer& fb, int width, int height)
{
    const int y_plane_size = width * height;
    const int uv_plane_size = (width / 2) * (height / 2);
    std::vector<uint8_t> yuv(static_cast<size_t>(y_plane_size + 2 * uv_plane_size));

    auto* y_plane = yuv.data();
    auto* u_plane = yuv.data() + y_plane_size;
    auto* v_plane = yuv.data() + y_plane_size + uv_plane_size;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color c = fb.get_pixel(x, y);
            // BT.601 coefficients
            const int Y = std::clamp(static_cast<int>(0.299f * c.r + 0.587f * c.g + 0.114f * c.b), 0, 255);
            y_plane[y * width + x] = static_cast<uint8_t>(Y);

            if (x % 2 == 0 && y % 2 == 0) {
                const int u_idx = (y / 2) * (width / 2) + (x / 2);
                const int U = std::clamp(static_cast<int>(-0.147f * c.r - 0.289f * c.g + 0.436f * c.b + 128.0f), 0, 255);
                const int V = std::clamp(static_cast<int>(0.615f * c.r - 0.515f * c.g - 0.100f * c.b + 128.0f), 0, 255);
                u_plane[u_idx] = static_cast<uint8_t>(U);
                v_plane[u_idx] = static_cast<uint8_t>(V);
            }
        }
    }
    return yuv;
}

} // namespace

// Forward declaration for registration function
void register_null_convert_sink(ExporterRegistry& registry);

namespace {

class NullConvertExporter final : public VideoExporter {
public:
    std::string_view id() const override { return "null-convert"; }
    std::string_view description() const override {
        return "Render + convert to YUV then discard (benchmark full pipeline)";
    }

    int export_video(const VideoExportJob& job) override {
        const Frame start = job.start;
        const Frame end   = job.end;
        const int total   = static_cast<int>(end - start);
        const auto& opts  = job.opts;
        const int width   = job.comp.width();
        const int height  = job.comp.height();

        spdlog::info("[null-convert] Rendering+converting {} frames [{}, {}) at {} fps — no output written",
                     total, start, end, opts.output.fps);

        profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
        profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

        const auto wall_t0 = profiling::now();

        // ── Create renderer ─────────────────────────────────────────────
        auto renderer = create_renderer(job.registry, job.settings);
        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
        if (!sw_renderer) {
            spdlog::error("[null-convert] Requires SoftwareRenderer backend");
            return 1;
        }

        // ── Warmup (optional) ───────────────────────────────────────────
        if (opts.warmup.warmup_renderer) {
            const auto warmup_t0 = profiling::now();
            runtime::warmup_renderer(*sw_renderer, job.comp, runtime::RendererWarmupOptions{
                .width = width,
                .height = height,
                .framebuffer_count = opts.warmup.warmup_framebuffers,
                .preallocate_framebuffers = true,
                .touch_memory = true,
                .render_dummy_frame = opts.warmup.warmup_dummy_frame,
                .dummy_frame = 0,
                .quiet = false,
            });
            const auto warmup_t1 = profiling::now();
            spdlog::info("[null-convert] Warmup: {:.1f} ms",
                         profiling::duration_ms(warmup_t0, warmup_t1));
        }

        // ── Render + convert loop ───────────────────────────────────────
        int frames_done = 0;
        double total_render_ms = 0.0;
        double total_convert_ms = 0.0;

        for (Frame f = start; f < end; ++f) {
            const auto t_render0 = profiling::now();
            auto fb = sw_renderer->render_frame(job.comp, f);
            const auto t_render1 = profiling::now();

            if (!fb) {
                spdlog::error("[null-convert] Render failed at frame {}", f);
                return 1;
            }

            // Convert RGBA → YUV (discard result)
            auto yuv = convert_rgba_to_yuv420p(*fb, width, height);
            const auto t_convert1 = profiling::now();

            // Prevent compiler from optimising away the conversion
            volatile uint8_t sink = yuv[0];
            (void)sink;

            const double render_ms = profiling::duration_ms(t_render0, t_render1);
            const double convert_ms = profiling::duration_ms(t_render1, t_convert1);
            total_render_ms += render_ms;
            total_convert_ms += convert_ms;

            ++frames_done;
            if (frames_done % std::max(1, total / 10) == 0 || frames_done == total) {
                spdlog::info("[null-convert]   {}/{} frames (render={:.1f}ms convert={:.1f}ms)",
                             frames_done, total, render_ms, convert_ms);
            }
        }

        const auto wall_t1 = profiling::now();
        const double wall_ms = profiling::duration_ms(wall_t0, wall_t1);

        spdlog::info("[null-convert] ✅ {} frames in {:.1f} ms ({:.1f} fps)",
                     frames_done, wall_ms,
                     frames_done / (wall_ms / 1000.0));
        spdlog::info("[null-convert]   Render avg: {:.2f} ms — Convert avg: {:.2f} ms",
                     total_render_ms / frames_done,
                     total_convert_ms / frames_done);

        return 0;
    }
};

} // namespace

void register_null_convert_sink(ExporterRegistry& registry) {
    registry.register_exporter(std::make_unique<NullConvertExporter>());
}

} // namespace chronon3d::cli
