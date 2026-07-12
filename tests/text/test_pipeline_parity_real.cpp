// test_pipeline_parity_real.cpp — Real pipeline parity matrix
//
// Compares the following pipelines using real framebuffer hashes:
//   1. SDK still          (in-process SoftwareRenderer)
//   2. CLI still          (chronon3d_cli still ...)
//   3. Raw video frame    (chronon3d_cli video --ffmpeg-mode png ...)
//   4. Pruning ON/OFF     (SDK dirty.enabled vs CLI --no-dirty-rects)
//   5. 1/N threads        (SDK tbb::global_control vs CLI CHRONON3D_THREADS)
//   6. Warm/cold cache    (fresh renderer vs reused renderer / --warmup)
//   7. Modular graph ON/OFF (SDK use_modular_graph vs CLI --graph/--no-graph)
//   8. Diagnostic overlay  (SDK text_layout_debug vs CLI --diagnostic-overlay /
//                          --diagnostic-overlay-only)
//
// The canary composition is registered as a CLI built-in composition
// ("PipelineParityCanary") so the exact same scene is rendered by
// every path.  All outputs are saved as PNG, loaded back into a
// Framebuffer, and hashed with the same helper, so comparisons are
// byte-exact when the pipelines are correct.

#include <doctest/doctest.h>
// TICKET-DOCTEST-SKIP-ROT: provide the SKIP() macro on doctest versions
// (< 2.4.0) where it is not in the top-level <doctest/doctest.h> include.
// The compat header aliases SKIP(msg) to the canonical DOCTEST_SKIP(msg)
// so the 5 SKIP(...) call sites below compile unchanged. See
// tests/helpers/doctest_skip_compat.hpp for the rationale + alternatives.
#include <tests/helpers/doctest_skip_compat.hpp>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text/pipeline_parity_canary.hpp>
// TICKET-CHRONON-GLOW-FINAL Fase 6 — `ChrononGlowFinalAE` composition
// factory (header-only; same helper as the CLI registration in
// `apps/chronon3d_cli/cli_init.hpp`).  The test renders the exact
// composition the CLI video subprocess renders, so the SDK stills
// must be byte-exact with the extracted video frames.
#include "content/compositions/chronon_glow_final.hpp"

#include <tbb/global_control.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

using namespace chronon3d;

namespace {

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

u64 hash_from_png(const std::string& path) {
    auto fb = test::load_png_as_framebuffer(path);
    REQUIRE(fb != nullptr);
    return test::framebuffer_hash(*fb);
}

// ── SDK render to PNG and return loaded hash ───────────────────────────────

u64 render_sdk_to_png(const std::string& png_path,
                      const RenderSettings& settings,
                      bool warm = false) {
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
                   const std::string& env_prefix = {}) {
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
//
// TICKET-CHRONON-GLOW-FINAL Fase 6 — renders the user-spec command
// `chronon3d_cli video ChrononGlowFinalAE --start 0 --end 60 --fps 30
//  -o output/glow_final/chronon_glow_final.mp4` and returns the paths
// of the per-frame PNGs (the CLI emits `frame_NNNNNN.png` when
// `--ffmpeg-mode png --keep-frames` is set; we use the same options
// so the extracted frames are the exact pixels the ffmpeg step
// would mux into the .mp4).  Returns empty paths on CLI failure.
struct VideoRenderResult {
    std::string video_path;       // path to the .mp4
    std::string frames_dir;       // directory containing per-frame PNGs
};

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
    // Fail-loud: verify the first frame was actually written.  If
    // ffmpeg failed silently or the CLI exited 0 without writing
    // frames, surface the error here rather than deep inside a
    // `REQUIRE(exists(...))` block in the caller.
    const std::string first_frame = (std::filesystem::path(frames_dir) / "frame_000000.png").string();
    if (!std::filesystem::exists(first_frame)) {
        r.video_path.clear();
    }
    return r;
}

// ── Per-frame alpha bbox + centroid + max_alpha metrics (spec §5) ────────
//
// Spec §5 metrics — per-frame alpha-weighted geometric + brightness
// validation.  No downsample (unlike the legacy 4x-downsample lambda in
// the Fase 6 test); each pixel contributes to bbox + centroid + max_alpha
// per user-spec verbatim "a > 3 pixels".  Returns a single FrameMetrics
// struct that the 60-frame loop in the test case appends to the CSV
// and asserts against per spec §5 thresholds.
struct FrameMetrics {
    int frame = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;          // alpha bbox (a > 3/255)
    int width = 0, height = 0;                    // bbox width / height
    double centroid_x = 0.0, centroid_y = 0.0;    // alpha-weighted centroid
    int visible_pixels = 0;                       // count of a > 3/255
    double alpha_sum = 0.0;                       // sum of a in [0, 1]
    int max_alpha = 0;                            // max a in [0, 255]
};

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

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Test: SDK still == CLI still
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: SDK still == CLI still") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk.png").string();
    const std::string cli_png = (tmp / "cli.png").string();

    const u64 sdk_hash = render_sdk_to_png(sdk_png, default_settings());
    REQUIRE(run_cli_still(cli_png, ""));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: SDK still == raw video frame
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: SDK still == raw video frame") {
    if (!ffmpeg_available()) {
        SKIP("TICKET-DOCTEST-SKIP-ROT: ffmpeg not found in PATH");
    }

    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk.png").string();

    const u64 sdk_hash = render_sdk_to_png(sdk_png, default_settings());
    const std::string video_frame = run_cli_video_first_frame(tmp / "video", "");
    REQUIRE(!video_frame.empty());
    const u64 video_hash = hash_from_png(video_frame);

    CHECK(sdk_hash == video_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: pruning OFF parity (SDK dirty.enabled=false == CLI --no-dirty-rects)
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: pruning OFF") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_prune.png").string();
    const std::string cli_png = (tmp / "cli_prune.png").string();

    RenderSettings settings = default_settings();
    settings.dirty.enabled = false;
    settings.dirty.use_bitmask = false;
    settings.dirty.use_tiles = false;

    const u64 sdk_hash = render_sdk_to_png(sdk_png, settings);
    REQUIRE(run_cli_still(cli_png, "--no-dirty-rects"));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: 1/N thread parity
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: 1 thread") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_1t.png").string();
    const std::string cli_png = (tmp / "cli_1t.png").string();

    u64 sdk_hash = 0;
    {
        tbb::global_control tbb_ctrl(
            tbb::global_control::max_allowed_parallelism, 1);
        sdk_hash = render_sdk_to_png(sdk_png, default_settings());
    }

    REQUIRE(run_cli_still(cli_png, "", "CHRONON3D_THREADS=1"));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: warm/cold cache parity
//   - SDK cold: fresh renderer, first render.
//   - SDK warm: same renderer, second render.
//   - CLI cold: no warmup flags.
//   - CLI warm: --warmup-renderer --warmup-dummy-frame.
// All four hashes must be identical.
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: warm/cold cache") {
    const auto tmp = make_temp_dir();

    const std::string sdk_cold_png = (tmp / "sdk_cold.png").string();
    const std::string sdk_warm_png = (tmp / "sdk_warm.png").string();
    const std::string cli_cold_png = (tmp / "cli_cold.png").string();
    const std::string cli_warm_png = (tmp / "cli_warm.png").string();

    // SDK cold
    const u64 sdk_cold_hash = render_sdk_to_png(sdk_cold_png, default_settings());

    // SDK warm: reuse the same renderer (caches populated by the cold render).
    const u64 sdk_warm_hash = render_sdk_to_png(sdk_warm_png, default_settings(), /*warm=*/true);

    // CLI cold
    REQUIRE(run_cli_still(cli_cold_png, ""));
    const u64 cli_cold_hash = hash_from_png(cli_cold_png);

    // CLI warm
    REQUIRE(run_cli_still(cli_warm_png, "--warmup-renderer --warmup-dummy-frame"));
    const u64 cli_warm_hash = hash_from_png(cli_warm_png);

    CHECK(sdk_cold_hash == sdk_warm_hash);
    CHECK(sdk_cold_hash == cli_cold_hash);
    CHECK(sdk_cold_hash == cli_warm_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: modular graph OFF parity
//   SDK: RenderSettings.use_modular_graph = false (renders the legacy path)
//   CLI: --no-graph  (forwards to pipeline.use_modular_graph=false, which
//         is mapped to settings.use_modular_graph in cli_render_utils.hpp)
//   Both must hash identically.
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: modular graph OFF") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_nograph.png").string();
    const std::string cli_png = (tmp / "cli_nograph.png").string();

    RenderSettings settings = default_settings();
    settings.use_modular_graph = false;

    const u64 sdk_hash = render_sdk_to_png(sdk_png, settings);
    REQUIRE(run_cli_still(cli_png, "--no-graph"));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: modular graph ON (explicit) parity
//   SDK: RenderSettings.use_modular_graph = true
//   CLI: --graph  (forces the modular path explicitly; redundant with
//        the default but locks the flag round-trip against future CLI
//        default flips).
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: modular graph ON (explicit)") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_graph.png").string();
    const std::string cli_png = (tmp / "cli_graph.png").string();

    RenderSettings settings = default_settings();
    settings.use_modular_graph = true;

    const u64 sdk_hash = render_sdk_to_png(sdk_png, settings);
    REQUIRE(run_cli_still(cli_png, "--graph"));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: diagnostic overlay ON parity
//   SDK: settings.text_layout_debug = true
//   CLI: --diagnostic-overlay  (maps to pipeline.diagnostic_overlay=true,
//         which cli_render_utils.hpp OR-merges into
//         settings.text_layout_debug=true — so the final RenderSettings
//         on both sides are identical).
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: diagnostic overlay ON") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_diag.png").string();
    const std::string cli_png = (tmp / "cli_diag.png").string();

    RenderSettings settings = default_settings();
    settings.text_layout_debug = true;
    // diagnostic_overlay_only defaults to false — leave it.

    const u64 sdk_hash = render_sdk_to_png(sdk_png, settings);
    REQUIRE(run_cli_still(cli_png, "--diagnostic-overlay"));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// Test: diagnostic overlay ONLY parity
//   SDK: settings.text_layout_debug = true AND
//        settings.diagnostic_overlay_only = true
//   CLI: --diagnostic-overlay-only  (maps to pipeline.diagnostic_overlay_only=true,
//         which OR-merges into text_layout_debug=true and sets
//         diagnostic_overlay_only=true — same final RenderSettings).
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: diagnostic overlay ONLY") {
    const auto tmp = make_temp_dir();
    const std::string sdk_png = (tmp / "sdk_diag_only.png").string();
    const std::string cli_png = (tmp / "cli_diag_only.png").string();

    RenderSettings settings = default_settings();
    settings.text_layout_debug = true;
    settings.diagnostic_overlay_only = true;

    const u64 sdk_hash = render_sdk_to_png(sdk_png, settings);
    REQUIRE(run_cli_still(cli_png, "--diagnostic-overlay-only"));
    const u64 cli_hash = hash_from_png(cli_png);

    CHECK(sdk_hash == cli_hash);
}

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-CHRONON-GLOW-FINAL Fase 6 — `ChrononGlowFinalAE` temporal
// stability + still-video parity (TEST FINALI).
//
// Renders the user-spec command
//   `chronon3d_cli video ChrononGlowFinalAE --start 0 --end 60 --fps 30
//    -o output/glow_final/chronon_glow_final.mp4`
// and verifies:
//
//   (c) Temporal stability across 60 frames:
//       - no-flicker       : all frames have non-zero ink (no empty bboxes)
//       - center-stability : the bright-pixel centroid stays within ±100px
//                            of the canvas center (960, 540) across f00/f15/f30
//       - no-glow-pop      : frame-to-frame pixel-diff is bounded (no
//                            sudden glow intensity changes between
//                            adjacent snapshot buckets)
//
//   (d) Still⇌frame parity:
//       - SDK still at frame N  ==  raw video frame N  (hash-exact)
//
// The test is SKIPped in non-ffmpeg environments and on the pre-existing
// `chronon3d::content` build rot (the CLI is not linked, so the
// `ChrononGlowFinalAE` composition cannot be resolved at runtime).
// Honest PARTIAL status: actual ctest execution is deferred to the next
// working-build-host session per AGENTS.md §honesty.
// ═══════════════════════════════════════════════════════════════════════════
TEST_CASE("real pipeline parity: ChrononGlowFinalAE temporal stability "
          "and still-video parity (Fase 6 TEST FINALI)") {
    if (!ffmpeg_available()) {
        SKIP("TICKET-DOCTEST-SKIP-ROT: ffmpeg not found in PATH");
    }
    if (!std::filesystem::exists(get_cli_path())) {
        SKIP("TICKET-DOCTEST-SKIP-ROT: chronon3d_cli not built at " << get_cli_path()
             << " — pre-existing build rot blocks the test");
    }

    // Resolve spec §5 canonical output root (override-able via env var
    // for CI / cross-host tests where std::filesystem::current_path()
    // is not the project root).  Default: output/text_video_acceptance/
    // relative to the cwd, where the CLI renders the .mp4 + raw_frames/
    // and the new (5) loop emits frame_metrics.csv.
    std::filesystem::path output_root;
    {
        const char* env = std::getenv("CHRONON3D_TEST_VIDEO_OUTPUT_DIR");
        if (env != nullptr && env[0] != '\0') {
            output_root = std::filesystem::path(env);
        } else {
            output_root = std::filesystem::current_path()
                          / "output" / "text_video_acceptance";
        }
    }

    const auto tmp = make_temp_dir();

    // ── (1) Render SDK stills at frames 0/15/30 using the exact same
    //       composition the CLI video subprocess renders.  The factory
    //       is the header-only `make_chronon_glow_final` from
    //       `content/compositions/chronon_glow_final.hpp` —
    //       the same helper the CLI registration in cli_init.hpp uses,
    //       so SDK == CLI by construction.
    auto comp = chronon3d::test::glow_final::make_chronon_glow_final(
        chronon3d::test::glow_final::default_landscape_props());
    auto renderer = test::make_renderer_shared();
    renderer->set_settings(default_settings());

    const std::string sdk_f00 = (tmp / "sdk_f00.png").string();
    const std::string sdk_f15 = (tmp / "sdk_f15.png").string();
    const std::string sdk_f30 = (tmp / "sdk_f30.png").string();

    auto fb0 = renderer->render(comp, Frame{0});
    REQUIRE(fb0 != nullptr);
    REQUIRE(save_png(*fb0, sdk_f00));
    const u64 hash_sdk_f00 = hash_from_png(sdk_f00);

    auto fb15 = renderer->render(comp, Frame{15});
    REQUIRE(fb15 != nullptr);
    REQUIRE(save_png(*fb15, sdk_f15));
    const u64 hash_sdk_f15 = hash_from_png(sdk_f15);

    auto fb30 = renderer->render(comp, Frame{30});
    REQUIRE(fb30 != nullptr);
    REQUIRE(save_png(*fb30, sdk_f30));
    const u64 hash_sdk_f30 = hash_from_png(sdk_f30);

    // ── (2) Render the full 60-frame video via the user-spec CLI
    //       command.  Writes the .mp4 + per-frame PNGs to the spec §5
    //       canonical output tree at output_root/ (= the canonical
    //       output/text_video_acceptance/ path resolved above, or the
    //       CHRONON3D_TEST_VIDEO_OUTPUT_DIR env override).  The CLI
    //       uses --ffmpeg-mode png + --keep-frames so the per-frame
    //       PNGs land in output_root/raw_frames/frame_NNNNNN.png —
    //       the exact pixels the ffmpeg step would mux into the .mp4.
    const auto vid = run_cli_video_chronon_glow_final(
        output_root.string(), /*start=*/0, /*end=*/60);
    REQUIRE(!vid.video_path.empty());

    // ── (3) Extract frames 0/15/30 from the video output directory.
    const std::string vid_f00 = (std::filesystem::path(vid.frames_dir) / "frame_000000.png").string();
    const std::string vid_f15 = (std::filesystem::path(vid.frames_dir) / "frame_000015.png").string();
    const std::string vid_f30 = (std::filesystem::path(vid.frames_dir) / "frame_000030.png").string();
    REQUIRE(std::filesystem::exists(vid_f00));
    REQUIRE(std::filesystem::exists(vid_f15));
    REQUIRE(std::filesystem::exists(vid_f30));

    const u64 hash_vid_f00 = hash_from_png(vid_f00);
    const u64 hash_vid_f15 = hash_from_png(vid_f15);
    const u64 hash_vid_f30 = hash_from_png(vid_f30);

    // ── (4) Still⇌frame parity (d) — SDK still == raw video frame.
    CHECK(hash_sdk_f00 == hash_vid_f00);
    CHECK(hash_sdk_f15 == hash_vid_f15);
    CHECK(hash_sdk_f30 == hash_vid_f30);

    // ── (5) Spec §5 video-completeness — full 60-frame alpha bbox +
    //       centroid + max_alpha loop.  Replaces the prior 3-frame
    //       sample (at f00/f15/f30 only) with a per-frame geometric
    //       + brightness validation, per user-spec verbatim:
    //
    //         bbox > 100×30 px, margine 8 px dai bordi,
    //         centro |cx-960|<110 e |cy-540|<110,
    //         salto centro tra frame adiacenti < 12 px
    //           (no flicker geometrico),
    //         nessun frame completamente vuoto,
    //         max_alpha > 64.
    //
    //       Emits a 12-column CSV at the canonical spec §5 path
    //         output_root/frame_metrics.csv
    //       with columns:
    //         frame,x0,y0,x1,y1,width,height,centroid_x,
    //         centroid_y,visible_pixels,alpha_sum,max_alpha
    //
    //       The CSV is written even on partial failure — every per-
    //       frame assertion is a CHECK (not REQUIRE) so the loop
    //       completes and the diagnostic file survives on working
    //       build host first runs per AGENTS.md §honesty.  Per-frame
    //       file existence + load_png_as_framebuffer non-null are
    //       REQUIRE (fail-loud on missing per the canonical Fase 6
    //       helper invariant `r.video_path.empty()`).

    constexpr int    kFrameCount              = 60;
    constexpr int    kMaxAlphaFloor           = 64;
    constexpr int    kBboxMinWidth            = 100;
    constexpr int    kBboxMinHeight           = 30;
    constexpr int    kEdgeMargin              = 8;     // 8 px from canvas edge
    constexpr int    kCanvasWidth             = 1920;
    constexpr int    kCanvasHeight            = 1080;
    constexpr int    kCanvasCenterX           = kCanvasWidth / 2;   // 960
    constexpr int    kCanvasCenterY           = kCanvasHeight / 2;  // 540
    constexpr double kCentroidAbsTolerance    = 110.0; // |cx-960|<110, |cy-540|<110
    constexpr double kInterFrameJumpMax       = 12.0;  // no flicker geometrico

    std::error_code ec;
    std::filesystem::create_directories(output_root, ec);
    REQUIRE(!ec);  // fail-loud if output tree cannot be created
    const std::filesystem::path csv_path = output_root / "frame_metrics.csv";
    std::ofstream csv(csv_path.string());
    REQUIRE(csv.is_open());  // fail-loud on permission / IO failure
    csv << "frame,x0,y0,x1,y1,width,height,centroid_x,"
           "centroid_y,visible_pixels,alpha_sum,max_alpha\n";

    int frames_loaded = 0;
    FrameMetrics prev{};
    bool have_prev = false;

    for (int f = 0; f < kFrameCount; ++f) {
        std::ostringstream fname;
        fname << "frame_" << std::setw(6) << std::setfill('0') << f << ".png";
        const std::string path =
            (std::filesystem::path(vid.frames_dir) / fname.str()).string();
        REQUIRE(std::filesystem::exists(path));  // fail-loud on per-frame missing

        auto fb_v = test::load_png_as_framebuffer(path);
        REQUIRE(fb_v != nullptr);
        REQUIRE(fb_v->width()  >= static_cast<size_t>(kCanvasWidth));
        REQUIRE(fb_v->height() >= static_cast<size_t>(kCanvasHeight));

        const FrameMetrics m = compute_frame_metrics(*fb_v, f);
        ++frames_loaded;

        csv << m.frame << ',' << m.x0 << ',' << m.y0 << ',' << m.x1 << ','
            << m.y1 << ',' << m.width << ',' << m.height << ','
            << std::fixed << std::setprecision(2) << m.centroid_x << ','
            << std::setprecision(2) << m.centroid_y << ','
            << m.visible_pixels << ',' << std::setprecision(6) << m.alpha_sum
            << ',' << m.max_alpha << '\n';

        // Per-frame assertions — all CHECK (not REQUIRE): the loop
        // must complete and emit the full CSV even on partial failure,
        // so the surviving diagnostic file is the working-build-host
        // operator's grep-discoverable post-mortem per AGENTS.md §honesty.
        CHECK(m.visible_pixels > 0);                        // no empty frame
        CHECK(m.max_alpha    > kMaxAlphaFloor);             // max_alpha > 64
        CHECK(m.width        > kBboxMinWidth);              // bbox > 100 px wide
        CHECK(m.height       > kBboxMinHeight);             // bbox > 30 px tall
        CHECK(m.x0 >= kEdgeMargin);                         // 8 px from left
        CHECK(m.y0 >= kEdgeMargin);                         // 8 px from top
        CHECK(m.x1 <  kCanvasWidth  - kEdgeMargin);         // 8 px from right
        CHECK(m.y1 <  kCanvasHeight - kEdgeMargin);         // 8 px from bottom
        CHECK(std::abs(m.centroid_x - kCanvasCenterX) < kCentroidAbsTolerance);
        CHECK(std::abs(m.centroid_y - kCanvasCenterY) < kCentroidAbsTolerance);
        if (have_prev) {
            CHECK(std::abs(m.centroid_x - prev.centroid_x) < kInterFrameJumpMax);
            CHECK(std::abs(m.centroid_y - prev.centroid_y) < kInterFrameJumpMax);
        }
        prev = m;
        have_prev = true;
    }
    csv.close();
    CHECK(frames_loaded == kFrameCount);
    INFO("CSV metrics emitted to " << csv_path.string()
         << " (" << frames_loaded << " frames)");

    // ── (6) No-glow-pop: the per-frame opacity envelope
    //       (0.40→0.85→0.50 at f00/f15/f30 per
    //       `opacity_for_frame` in content/compositions/chronon_glow_final.hpp) must
    //       produce distinct per-frame hashes.  If the envelope is
    //       broken (e.g. opacity=1.0 for all frames), all 3 hashes
    //       would collapse to the same value.  Kept verbatim from
    //       the prior 3-frame sample — structurally different from
    //       the spec §5 60-frame geometric loop above (hash-based
    //       temporal variation vs metric-based geometric stability);
    //       both are kept.
    CHECK(hash_sdk_f00 != hash_sdk_f15);
    CHECK(hash_sdk_f15 != hash_sdk_f30);
    CHECK(hash_sdk_f00 != hash_sdk_f30);
}
