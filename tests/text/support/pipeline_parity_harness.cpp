// ============================================================================
// tests/text/support/pipeline_parity_harness.cpp
//
// TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — harness implementation.
//
// All helpers were previously in an anonymous namespace inside the
// monolithic test_pipeline_parity_real.cpp.  The split moves them into
// the chronon3d::test::parity_harness namespace (see header) so the 4
// dedicated test files (still / video / runtime_modes /
// chronon_glow_temporal) can share the same subprocess + hash pipeline
// without duplication.
//
// Build target: chronon3d_pipeline_parity_real_tests (see
// tests/pipeline_parity_real_tests.cmake SOURCES list).
// ============================================================================

#include "pipeline_parity_harness.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text/pipeline_parity_canary.hpp>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <thread>

namespace chronon3d::test::parity_harness {

using namespace chronon3d;

// ── CLI path ─────────────────────────────────────────────────────────────

std::string get_cli_path() {
#ifdef CHRONON3D_CLI_PATH
    return CHRONON3D_CLI_PATH;
#else
    // Best-effort fallback: assume the test runs from the project root
    // and the CLI was built with the default preset.
    return "build/chronon3d_cli";
#endif
}

// ── Temporary directory helper ─────────────────────────────────────────────

std::filesystem::path make_temp_dir() {
    static int counter = 0;
    std::ostringstream oss;
    oss << "chronon3d_pipeline_parity_real_"
        << std::this_thread::get_id() << "_" << (++counter);
    auto path = std::filesystem::temp_directory_path() / oss.str();
    std::filesystem::create_directories(path);
    return path;
}

// ── Subprocess helper ──────────────────────────────────────────────────────

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

bool ffmpeg_available() {
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
}

// ── Hash from PNG ──────────────────────────────────────────────────────────

std::uint64_t hash_from_png(const std::string& path) {
    auto fb = test::load_png_as_framebuffer(path);
    REQUIRE(fb != nullptr);
    return test::framebuffer_hash(*fb);
}

// ── SDK render to PNG and return loaded hash ───────────────────────────────

std::uint64_t render_sdk_to_png(const std::string& png_path,
                               const RenderSettings& settings,
                               bool warm) {
    auto renderer = test::make_renderer_shared();
    renderer->set_settings(settings);

    Composition comp = test::make_pipeline_parity_canary({});

    // Optional warm-up render: prime all caches before saving.
    if (warm) {
        [[maybe_unused]] auto _ = renderer->render(comp, Frame{0});
    }

    auto fb = renderer->render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(save_png(*fb, png_path));

    return hash_from_png(png_path);
}

// ── CLI still ─────────────────────────────────────────────────────────────

bool run_cli_still(const std::string& out_png,
                   const std::string& extra_args,
                   const std::string& env_prefix) {
    std::ostringstream cmd;
    if (!env_prefix.empty()) {
        cmd << env_prefix << " ";
    }
    cmd << get_cli_path() << " still PipelineParityCanary"
        << " --skip-preflight"
        << " -o " << std::filesystem::path(out_png).string()
        << " " << extra_args
        << " > /dev/null 2>&1";
    return run_command(cmd.str()) == 0;
}

// ── CLI video first frame ──────────────────────────────────────────────────

std::string run_cli_video_first_frame(const std::string& work_dir,
                                      const std::string& extra_args) {
    std::filesystem::path wd(work_dir);
    std::filesystem::create_directories(wd);
    std::string mp4_out = (wd / "out.mp4").string();
    std::string frames_dir = (wd / "frames").string();

    std::ostringstream cmd;
    cmd << get_cli_path() << " video PipelineParityCanary"
        << " -o " << mp4_out
        << " --start 0 --end 1 --fps 30"
        << " --ffmpeg-mode png --keep-frames"
        << " --frames-dir " << frames_dir
        << " " << extra_args
        << " > /dev/null 2>&1";
    if (run_command(cmd.str()) != 0) {
        return {};
    }

    std::string first_frame = (std::filesystem::path(frames_dir) / "frame_000000.png").string();
    if (!std::filesystem::exists(first_frame)) {
        return {};
    }
    return first_frame;
}

// ── CLI video full-sequence render for ChrononGlowFinalAE ────────────────

VideoRenderResult run_cli_video_chronon_glow_final(
        const std::string& work_dir,
        int start_frame,
        int end_frame) {
    std::filesystem::path wd(work_dir);
    std::filesystem::create_directories(wd);
    std::string mp4_out = (wd / "chronon_glow_final.mp4").string();
    std::string frames_dir = (wd / "raw_frames").string();
    std::filesystem::create_directories(frames_dir);

    std::ostringstream cmd;
    cmd << get_cli_path() << " video ChrononGlowFinalAE"
        << " -o " << mp4_out
        << " --start " << start_frame
        << " --end " << end_frame
        << " --fps 30"
        << " --ffmpeg-mode png --keep-frames"
        << " --frames-dir " << frames_dir
        << " > /dev/null 2>&1";
    const int rc = run_command(cmd.str());
    VideoRenderResult r{mp4_out, frames_dir};
    if (rc != 0) {
        r.video_path.clear();
        return r;
    }
    // Fail-loud: verify the first frame was actually written.
    const std::string first_frame = (std::filesystem::path(frames_dir) / "frame_000000.png").string();
    if (!std::filesystem::exists(first_frame)) {
        r.video_path.clear();
    }
    return r;
}

// ── Per-frame alpha bbox + centroid + max_alpha metrics (spec §5) ────────

FrameMetrics compute_frame_metrics(const Framebuffer& fb, int frame_num) {
    constexpr int   kVisibleAlphaThreshold = 3;     // "a > 3 pixels" → a > 3/255
    constexpr float kCentroidEpsilon      = 1e-6f;
    FrameMetrics m;
    m.frame = frame_num;
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());
    int min_x = w, min_y = h, max_x = -1, max_y = -1;
    double sx = 0.0, sy = 0.0, sa = 0.0;
    double sum_a = 0.0;
    int max_a = 0;
    int vis = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = fb.get_pixel(x, y).a;
            const int   a_int = static_cast<int>(a * 255.0f);
            sum_a += static_cast<double>(a);
            if (a_int > max_a) max_a = a_int;
            if (a_int > kVisibleAlphaThreshold) {
                ++vis;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
                sx += static_cast<double>(x) * static_cast<double>(a);
                sy += static_cast<double>(y) * static_cast<double>(a);
                sa += static_cast<double>(a);
            }
        }
    }
    if (min_x < w) m.x0 = min_x;
    if (min_y < h) m.y0 = min_y;
    if (max_x >= 0) m.x1 = max_x;
    if (max_y >= 0) m.y1 = max_y;
    m.width        = (m.x1 > m.x0) ? (m.x1 - m.x0 + 1) : 0;
    m.height       = (m.y1 > m.y0) ? (m.y1 - m.y0 + 1) : 0;
    m.centroid_x   = sa > static_cast<double>(kCentroidEpsilon) ? (sx / sa) : 0.0;
    m.centroid_y   = sa > static_cast<double>(kCentroidEpsilon) ? (sy / sa) : 0.0;
    m.visible_pixels = vis;
    m.alpha_sum    = sum_a;
    m.max_alpha    = max_a;
    return m;
}

// ── Default render settings ─────────────────────────────────────────────────

RenderSettings default_settings() {
    RenderSettings s;
    s.use_modular_graph = true;
    return s;
}

} // namespace chronon3d::test::parity_harness
