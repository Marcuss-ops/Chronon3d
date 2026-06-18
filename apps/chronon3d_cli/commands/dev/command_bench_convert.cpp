#include "../../commands.hpp"
#include "../../utils/job/render_job.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <chronon3d/media/frame_conversion/frame_converter.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chronon3d/core/profiling/profiling.hpp>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace chronon3d::cli {

using namespace video;

// ── BenchConvertArgs is defined in commands.hpp ──

static uint64_t now_ns() {
    return profiling::timestamp_ns();
}

// ==========================================================================
//  Helper: compute total bytes for YUV420P / NV12
// ==========================================================================

static uint64_t yuv_bytes(int w, int h, const std::string& fmt_name) {
    if (fmt_name == "nv12") {
        // NV12: Y plane (w*h) + UV plane (w*h/2) = 1.5 * w*h bytes
        return static_cast<uint64_t>(w) * h * 3 / 2;
    }
    // YUV420P: Y plane (w*h) + U plane (w*h/4) + V plane (w*h/4) = 1.5 * w*h
    return static_cast<uint64_t>(w) * h * 3 / 2;
}

// ==========================================================================
//  Helper: count mismatched bytes between two buffers
// ==========================================================================

static uint64_t count_mismatched_bytes(const uint8_t* a, const uint8_t* b, uint64_t len) {
    uint64_t mismatches = 0;
    for (uint64_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) ++mismatches;
    }
    return mismatches;
}

static std::string normalize_output_format(std::string fmt) {
    std::transform(fmt.begin(), fmt.end(), fmt.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (fmt == "yuv" || fmt == "420p" || fmt == "yuv420" || fmt == "yuv420p8") {
        return "yuv420p";
    }
    if (fmt == "nv12" || fmt == "nv-12") {
        return "nv12";
    }
    return fmt;
}

static double max_abs_diff(const uint8_t* a, const uint8_t* b, uint64_t len) {
    double max_diff = 0.0;
    for (uint64_t i = 0; i < len; ++i) {
        double diff = std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (diff > max_diff) max_diff = diff;
    }
    return max_diff;
}

// ==========================================================================
//  Run a single conversion and return timing
// ==========================================================================

struct BenchResult {
    std::string name;
    double mean_ns{0};
    double gb_s{0};
    uint64_t bytes_mismatch{0};
    double max_channel_diff{0};
    bool used_simd{false};  // legacy column (set when backend == HighwayDirect for backwards compatibility)
};

static BenchResult run_swscale_bench(
    const Framebuffer& fb,
    int w, int h,
    const std::string& fmt_name,
    bool apply_gamma,
    int iterations,
    std::vector<uint8_t>& work_buf)
{
    const uint64_t total_bytes = yuv_bytes(w, h, fmt_name);
    uint64_t total_ns = 0;

    const bool is_yuv = (fmt_name == "yuv420p");
    const auto fmt = is_yuv ? video::EncoderPixelFormat::YUV420P
                            : video::EncoderPixelFormat::NV12;
    uint8_t* y_ptr  = work_buf.data();
    uint8_t* u_ptr  = is_yuv ? work_buf.data() + static_cast<size_t>(w) * h : nullptr;
    uint8_t* v_ptr  = is_yuv ? work_buf.data() + static_cast<size_t>(w) * h * 5 / 4 : nullptr;
    uint8_t* uv_ptr = is_yuv ? nullptr : work_buf.data() + static_cast<size_t>(w) * h;

    for (int i = 0; i < iterations; ++i) {
        std::memset(work_buf.data(), 0, work_buf.size());
        const uint64_t t0 = now_ns();

        video::ConvertFrameRequest creq{
            .src = fb,
            .planes = video::FramePlanes{
                .y = y_ptr, .u = u_ptr, .v = v_ptr, .uv = uv_ptr,
                .stride_y = w, .stride_u = w/2, .stride_v = w/2, .stride_uv = w,
            },
            .width = w,
            .height = h,
            .format = fmt,
            .matrix = video::YuvMatrix::BT709,
            .range = video::ColorRange::Limited,
            .apply_gamma = apply_gamma,
        };
        if (fmt_name == "yuv420p")
            video::convert_rgba_to_yuv420p_swscale(creq);
        else
            video::convert_rgba_to_nv12_swscale(creq);

        total_ns += now_ns() - t0;
    }

    const double mean_ns = static_cast<double>(total_ns) / iterations;
    const double wall_s  = mean_ns / 1e9;
    const double gb_s    = wall_s > 0.0
        ? (static_cast<double>(total_bytes) / wall_s) / 1e9
        : 0.0;

    return BenchResult{
        .name = "sws_scale",
        .mean_ns = mean_ns,
        .gb_s = gb_s,
    };
}

// ==========================================================================
//  Public entry point: command_bench_convert
// ==========================================================================

int command_bench_convert(const CompositionRegistry& registry, const BenchConvertArgs& args) {
    const std::string fmt = normalize_output_format(args.format);
    if (fmt != "yuv420p" && fmt != "nv12") {
        spdlog::error(
            "Unsupported format: {} — use yuv420p or nv12. If you meant a video container, use the video command instead.",
            args.format
        );
        return 1;
    }

    // ── Render a single frame ──────────────────────────────────────────
    RenderArgs render_args;
    render_args.comp_id = args.comp_id;
    render_args.frames  = std::to_string(args.frame);

    auto plan = plan_render_job(registry, render_args);
    if (!plan) {
        spdlog::error("Failed to plan render job for '{}'", args.comp_id);
        return 1;
    }

    auto renderer = create_renderer(registry, plan->settings);
    const Frame frame{args.frame};
    spdlog::info("Rendering frame {} of '{}' for conversion benchmark...", frame, plan->comp_id);

    auto fb = renderer->render_frame(*plan->comp, frame);
    if (!fb) {
        spdlog::error("Failed to render frame {}", frame);
        return 1;
    }

    const int w = plan->comp->width();
    const int h = plan->comp->height();
    if (w % 2 != 0 || h % 2 != 0) {
        spdlog::error("Frame dimensions must be even for YUV420P/NV12 (got {}x{})", w, h);
        return 1;
    }

    const uint64_t yuv_total = yuv_bytes(w, h, fmt);
    spdlog::info("Frame: {}x{}  Format: {}  Bytes/frame: {}  Iterations: {}  Gamma: {}",
                 w, h, fmt, yuv_total, args.iterations,
                 args.apply_gamma ? "sRGB" : "linear");

    // ── Pre-allocate output buffer ─────────────────────────────────────
    std::vector<uint8_t> work_buf(yuv_total);

    // ── sws_scale benchmark ────────────────────────────────────────────
    auto sws = run_swscale_bench(*fb, w, h, fmt,
                                 args.apply_gamma, args.iterations,
                                 work_buf);

    // ── Print results ──────────────────────────────────────────────────
    std::ostringstream out;
    out << "\n"
        << "═══════════════════════════════════════════════\n"
        << "  FRAME CONVERSION (swscale) — " << w << "×" << h << " "
        << fmt << " ×" << args.iterations << " iters\n"
        << "═══════════════════════════════════════════════\n\n"
        << "  ms/frame: " << fmt::format("{:.3f}", sws.mean_ns / 1e6) << "\n"
        << "  GB/s:     " << fmt::format("{:.2f}", sws.gb_s) << "\n"
        << "\n═══════════════════════════════════════════════\n";
    spdlog::info("{}", out.str());

    return 0;
}

} // namespace chronon3d::cli
