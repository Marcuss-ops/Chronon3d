#include "../../commands.hpp"
#include "../../utils/job/render_job.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/direct_yuv_converter.hpp>
#include "src/media/frame_conversion/internal/yuv_conversion_internal.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chronon3d/core/profiling/profiling.hpp>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

// Forward declarations for libyuv — C linkage, no header conflicts.
extern "C" {
int ABGRToI420(const uint8_t* src_abgr, int src_stride_abgr,
               uint8_t* dst_y, int dst_stride_y,
               uint8_t* dst_u, int dst_stride_u,
               uint8_t* dst_v, int dst_stride_v,
               int width, int height);
int ABGRToNV12(const uint8_t* src_abgr, int src_stride_abgr,
               uint8_t* dst_y, int dst_stride_y,
               uint8_t* dst_uv, int dst_stride_uv,
               int width, int height);
}

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
    bool used_simd{false};
};

static BenchResult run_bench_path(
    const std::string& name,
    const Framebuffer& fb,
    int w, int h,
    const std::string& fmt_name,
    bool apply_gamma,
    int iterations,
    const std::vector<uint8_t>& ref_buf,  // HWY reference for mismatch
    std::vector<uint8_t>& work_buf)       // reusable output buffer
{
    const uint64_t total_bytes = yuv_bytes(w, h, fmt_name);
    uint64_t total_ns = 0;

    for (int i = 0; i < iterations; ++i) {
        std::memset(work_buf.data(), 0, work_buf.size());
        const uint64_t t0 = now_ns();

        // Dispatch to the named path
        if (name == "direct_hwy_yuv") {
            DirectYuvRequest dreq{
                .src = fb,
                .dst_y = work_buf.data(),
                .dst_u = (fmt_name == "yuv420p") ? work_buf.data() + static_cast<size_t>(w) * h : nullptr,
                .dst_v = (fmt_name == "yuv420p") ? work_buf.data() + static_cast<size_t>(w) * h * 5 / 4 : nullptr,
                .dst_uv = (fmt_name == "nv12") ? work_buf.data() + static_cast<size_t>(w) * h : nullptr,
                .dst_stride_y = w,
                .dst_stride_u = w / 2,
                .dst_stride_v = w / 2,
                .dst_stride_uv = w,
                .width = w,
                .height = h,
                .format = (fmt_name == "nv12") ? video::EncoderPixelFormat::NV12
                                                : video::EncoderPixelFormat::YUV420P,
                .apply_gamma = apply_gamma,
            };
            if (fmt_name == "yuv420p")
                video::convert_to_yuv420p_hwy(dreq);
            else
                video::convert_to_nv12_hwy(dreq);

        } else if (name == "direct_tbb_yuv") {
            DirectYuvRequest dreq{
                .src = fb,
                .dst_y = work_buf.data(),
                .dst_u = (fmt_name == "yuv420p") ? work_buf.data() + static_cast<size_t>(w) * h : nullptr,
                .dst_v = (fmt_name == "yuv420p") ? work_buf.data() + static_cast<size_t>(w) * h * 5 / 4 : nullptr,
                .dst_uv = (fmt_name == "nv12") ? work_buf.data() + static_cast<size_t>(w) * h : nullptr,
                .dst_stride_y = w,
                .dst_stride_u = w / 2,
                .dst_stride_v = w / 2,
                .dst_stride_uv = w,
                .width = w,
                .height = h,
                .format = (fmt_name == "nv12") ? video::EncoderPixelFormat::NV12
                                                : video::EncoderPixelFormat::YUV420P,
                .apply_gamma = apply_gamma,
            };
            if (fmt_name == "yuv420p")
                video::convert_to_yuv420p_parallel(dreq);
            else
                video::convert_to_nv12_parallel(dreq);

        } else if (name == "sws_scale") {
            video::ConvertFrameRequest creq{
                .src = fb,
                .dst_y = work_buf.data(),
                .dst_u = (fmt_name == "yuv420p") ? work_buf.data() + static_cast<size_t>(w) * h : nullptr,
                .dst_v = (fmt_name == "yuv420p") ? work_buf.data() + static_cast<size_t>(w) * h * 5 / 4 : nullptr,
                .dst_uv = (fmt_name == "nv12") ? work_buf.data() + static_cast<size_t>(w) * h : nullptr,
                .dst_stride_y = w,
                .dst_stride_uv = w,
                .dst_stride_u = w / 2,
                .dst_stride_v = w / 2,
                .color_matrix = 0,
                .width = w,
                .height = h,
                .format = (fmt_name == "nv12") ? video::EncoderPixelFormat::NV12
                                                : video::EncoderPixelFormat::YUV420P,
                .apply_gamma = apply_gamma,
            };
            if (fmt_name == "yuv420p")
                video::convert_rgba_to_yuv420p_swscale(creq);
            else
                video::convert_rgba_to_nv12_swscale(creq);

        } else if (name == "libyuv") {
            // Float framebuffer → RGBA8 staging → libyuv ABGRToI420/NV12.
            // (Our byte order R,G,B,A = libyuv ABGR).
            const size_t rgba_bytes = static_cast<size_t>(w) * h * 4;
            thread_local std::vector<uint8_t> rgba_buf;
            if (rgba_buf.size() < rgba_bytes) rgba_buf.resize(rgba_bytes);

            // Serial float→RGBA8 conversion (no parallel_for needed for benchmark)
            const Color* src_data = fb.data();
            const int src_stride = fb.allocated_width();
            for (int y = 0; y < h; ++y) {
                const Color* src_row = src_data + static_cast<size_t>(y) * src_stride;
                uint8_t* dst_row = rgba_buf.data() + static_cast<size_t>(y) * w * 4;
                for (int x = 0; x < w; ++x) {
                    const Color& c = src_row[x];
                    if (apply_gamma) {
                        dst_row[x * 4 + 0] = Color::linear_to_srgb8(c.r);
                        dst_row[x * 4 + 1] = Color::linear_to_srgb8(c.g);
                        dst_row[x * 4 + 2] = Color::linear_to_srgb8(c.b);
                    } else {
                        auto to8 = [](float v) -> uint8_t {
                            return static_cast<uint8_t>(
                                std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                        };
                        dst_row[x * 4 + 0] = to8(c.r);
                        dst_row[x * 4 + 1] = to8(c.g);
                        dst_row[x * 4 + 2] = to8(c.b);
                    }
                    dst_row[x * 4 + 3] = static_cast<uint8_t>(
                        std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }

            // RGBA8 → YUV via libyuv
            const int src_stride_rgba = w * 4;
            const int dst_stride_y = w;
            if (fmt_name == "yuv420p") {
                uint8_t* dst_u = work_buf.data() + static_cast<size_t>(w) * h;
                uint8_t* dst_v = work_buf.data() + static_cast<size_t>(w) * h * 5 / 4;
                ABGRToI420(
                    rgba_buf.data(), src_stride_rgba,
                    work_buf.data(), dst_stride_y,
                    dst_u, w / 2,
                    dst_v, w / 2,
                    w, h);
            } else {
                uint8_t* dst_uv = work_buf.data() + static_cast<size_t>(w) * h;
                ABGRToNV12(
                    rgba_buf.data(), src_stride_rgba,
                    work_buf.data(), dst_stride_y,
                    dst_uv, w,
                    w, h);
            }
        }

        total_ns += now_ns() - t0;
    }

    const double mean_ns = static_cast<double>(total_ns) / iterations;
    const double wall_s  = mean_ns / 1e9;
    const double gb_s    = wall_s > 0.0
        ? (static_cast<double>(total_bytes) / wall_s) / 1e9
        : 0.0;

    BenchResult result;
    result.name      = name;
    result.mean_ns   = mean_ns;
    result.gb_s      = gb_s;
    result.used_simd = (name == "direct_hwy_yuv" || name == "libyuv");
    if (name != "direct_hwy_yuv" && !ref_buf.empty()) {
        result.bytes_mismatch   = count_mismatched_bytes(ref_buf.data(), work_buf.data(), total_bytes);
        result.max_channel_diff = max_abs_diff(ref_buf.data(), work_buf.data(), total_bytes);
    }
    return result;
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

    // ── Pre-allocate output buffers ────────────────────────────────────
    std::vector<uint8_t> ref_buf(yuv_total);
    std::vector<uint8_t> work_buf(yuv_total);

    // ── Phase 1: HWY reference run (also warms caches) ─────────────────
    BenchResult hwy = run_bench_path("direct_hwy_yuv", *fb, w, h, fmt,
                                     args.apply_gamma, 1, ref_buf, ref_buf);
    hwy.name = "direct_hwy_yuv";

    // Run remaining HWY iterations for timing (with warm cache)
    {
        auto timing = run_bench_path("direct_hwy_yuv", *fb, w, h, fmt,
                                     args.apply_gamma, args.iterations,
                                     ref_buf, work_buf);
        hwy.mean_ns   = timing.mean_ns;
        hwy.gb_s      = timing.gb_s;
        hwy.used_simd = true;
    }

    // ── Phase 2: TBB scalar ────────────────────────────────────────────
    auto tbb = run_bench_path("direct_tbb_yuv", *fb, w, h, fmt,
                              args.apply_gamma, args.iterations,
                              ref_buf, work_buf);

    // ── Phase 3: sws_scale (RGBA8 staging) ─────────────────────────────
    auto sws = run_bench_path("sws_scale", *fb, w, h, fmt,
                              args.apply_gamma, args.iterations,
                              ref_buf, work_buf);

    // ── Phase 4: libyuv (RGBA8 staging + libyuv ABGRToI420/NV12) ───────
    auto libyuv_res = run_bench_path("libyuv", *fb, w, h, fmt,
                                     args.apply_gamma, args.iterations,
                                     ref_buf, work_buf);

    // ── Print results table ────────────────────────────────────────────
    const auto col_name = std::setw(17);
    const auto col_val  = std::setw(12);
    const auto col_unit = std::setw(10);

    std::ostringstream out;
    out << "\n"
        << "═══════════════════════════════════════════════════════════════════════\n"
        << "  FRAME CONVERSION BENCHMARK  —  " << w << "×" << h << "  "
        << fmt << "  ×" << args.iterations << " iters\n"
        << "═══════════════════════════════════════════════════════════════════════\n\n"
        << std::left << col_name << "Path"
        << std::right << col_val << "ms/frame"
        << col_val << "GB/s"
        << col_val << "SIMD"
        << col_val << "Mismatch"
        << col_unit << "MaxΔ"
        << "\n"
        << std::string(75, '-') << "\n";

    auto print_row = [&](const BenchResult& r) {
        out << std::left << col_name << r.name
            << std::right << col_val << fmt::format("{:.3f}", r.mean_ns / 1e6)
            << col_val << fmt::format("{:.2f}", r.gb_s)
            << col_val << (r.used_simd ? "YES" : "no")
            << col_val;
        if (r.name == "direct_hwy_yuv") {
            out << "— (ref)";
        } else {
            const double mismatch_pct = yuv_total > 0
                ? (static_cast<double>(r.bytes_mismatch) / static_cast<double>(yuv_total)) * 100.0
                : 0.0;
            out << fmt::format("{:.2f}%", mismatch_pct);
        }
        out << col_unit;
        if (r.name == "direct_hwy_yuv") {
            out << "—";
        } else {
            out << fmt::format("{:.1f}", r.max_channel_diff);
        }
        out << "\n";
    };

    print_row(hwy);
    print_row(tbb);
    print_row(sws);
    print_row(libyuv_res);

    // ── Speedup ratios ─────────────────────────────────────────────────
    if (hwy.mean_ns > 0 && tbb.mean_ns > 0) {
        const double speedup = tbb.mean_ns / hwy.mean_ns;
        out << "\n  HWY SIMD vs TBB scalar:  "
            << fmt::format("{:.2f}× faster", speedup);
        if (tbb.bytes_mismatch == 0)
            out << "  (pixel-identical)";
        out << "\n";
    }
    if (hwy.mean_ns > 0 && sws.mean_ns > 0) {
        const double speedup = sws.mean_ns / hwy.mean_ns;
        out << "  HWY SIMD vs sws_scale:   "
            << fmt::format("{:.2f}× faster", speedup);
        if (sws.bytes_mismatch > 0) {
            const double mismatch_pct = yuv_total > 0
                ? (static_cast<double>(sws.bytes_mismatch) / static_cast<double>(yuv_total)) * 100.0
                : 0.0;
            out << "  (" << fmt::format("{:.2f}%", mismatch_pct) << " bytes differ)";
        }
        out << "\n";
    }
    if (hwy.mean_ns > 0 && libyuv_res.mean_ns > 0) {
        const double speedup = libyuv_res.mean_ns / hwy.mean_ns;
        out << "  HWY SIMD vs libyuv:      "
            << fmt::format("{:.2f}× faster", speedup);
        if (libyuv_res.bytes_mismatch == 0) {
            out << "  (pixel-identical)";
        } else {
            const double mismatch_pct = yuv_total > 0
                ? (static_cast<double>(libyuv_res.bytes_mismatch) / static_cast<double>(yuv_total)) * 100.0
                : 0.0;
            out << "  (" << fmt::format("{:.2f}%", mismatch_pct) << " bytes differ)";
        }
        out << "\n";
    }

    out << "\n═══════════════════════════════════════════════════════════════════════\n";
    spdlog::info("{}", out.str());

    return 0;
}

} // namespace chronon3d::cli
