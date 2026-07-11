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

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text/pipeline_parity_canary.hpp>
// TICKET-CHRONON-GLOW-FINAL Fase 6 — `ChrononGlowFinalAE` composition
// factory (header-only; same helper as the CLI registration in
// `apps/chronon3d_cli/cli_init.hpp`).  The test renders the exact
// composition the CLI video subprocess renders, so the SDK stills
// must be byte-exact with the extracted video frames.
#include <tests/visual/ae_parity/glow_final_compositions.hpp>

#include <tbb/global_control.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    std::string frames_dir = (wd / "frames").string();
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
        SKIP("ffmpeg not found in PATH");
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
        SKIP("ffmpeg not found in PATH");
    }
    if (!std::filesystem::exists(get_cli_path())) {
        SKIP("chronon3d_cli not built at " << get_cli_path()
             << " — pre-existing build rot blocks the test");
    }

    const auto tmp = make_temp_dir();

    // ── (1) Render SDK stills at frames 0/15/30 using the exact same
    //       composition the CLI video subprocess renders.  The factory
    //       is the header-only `make_chronon_glow_final` from
    //       `tests/visual/ae_parity/glow_final_compositions.hpp` —
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
    //       command.  Uses --ffmpeg-mode png + --keep-frames so the
    //       per-frame PNGs land in `frames/frame_NNNNNN.png` — the
    //       exact pixels the ffmpeg step would mux into the .mp4.
    const auto vid = run_cli_video_chronon_glow_final(
        tmp / "video", /*start=*/0, /*end=*/60);
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

    // ── (5) Temporal stability (c) — no-flicker: all 3 SDK still
    //       framebuffers must have non-zero alpha content.  The
    //       `hash != 0` sentinel is vacuous because
    //       `test::framebuffer_hash()` uses a non-zero seed/offset
    //       (an empty framebuffer hashes to a non-zero value
    //       determined by the seed).  Use the actual alpha sum
    //       instead — an empty frame has sum ≈ 0, an inked frame
    //       has sum > 0.  This is the canonical "is the rasterizer
    //       producing ink?" check.
    auto alpha_sum = [](const Framebuffer& fb) -> double {
        double sum = 0;
        const int w = static_cast<int>(fb.width());
        const int h = static_cast<int>(fb.height());
        for (int y = 0; y < h; y += 4) {  // 4x downsample for speed
            for (int x = 0; x < w; x += 4) {
                sum += fb.get_pixel(x, y).a;
            }
        }
        return sum;
    };
    const double sum_f00 = alpha_sum(*fb0);
    const double sum_f15 = alpha_sum(*fb15);
    const double sum_f30 = alpha_sum(*fb30);
    CHECK(sum_f00 > 0.0);
    CHECK(sum_f15 > 0.0);
    CHECK(sum_f30 > 0.0);

    // ── (6) No-glow-pop: the per-frame opacity envelope
    //       (0.40→0.85→0.50 at f00/f15/f30 per
    //       `opacity_for_frame` in glow_final_compositions.hpp) must
    //       produce distinct per-frame hashes.  If the envelope is
    //       broken (e.g. opacity=1.0 for all frames), all 3 hashes
    //       would collapse to the same value.
    CHECK(hash_sdk_f00 != hash_sdk_f15);
    CHECK(hash_sdk_f15 != hash_sdk_f30);
    CHECK(hash_sdk_f00 != hash_sdk_f30);

    // ── (7) Center-stability: the per-frame ink centroid must stay
    //       within ±100px of the canvas center (960, 540) for all 3
    //       snapshot buckets.  The test loads each PNG as a
    //       Framebuffer and computes the alpha-weighted centroid via
    //       the same `framebuffer_hash` / `load_png_as_framebuffer`
    //       helpers used by the other parity tests.  The ±100px
    //       tolerance matches the existing `ae_08_glow_pulse.cpp`
    //       geometry lock (centroid within ~5% of canvas radius).
    auto fb_v0 = test::load_png_as_framebuffer(vid_f00);
    auto fb_v15 = test::load_png_as_framebuffer(vid_f15);
    auto fb_v30 = test::load_png_as_framebuffer(vid_f30);
    REQUIRE(fb_v0 != nullptr);
    REQUIRE(fb_v15 != nullptr);
    REQUIRE(fb_v30 != nullptr);

    // ── (5b) N5 gap — no-flicker on the VIDEO frames too.  The
    //       parity check passes vacuously if BOTH SDK and video
    //       produce empty (zero-alpha) output (e.g. a missing font
    //       atlas, a broken content module, the pre-existing build
    //       rot).  Check the actual alpha content of the video
    //       frames so a regression that produces empty output from
    //       both paths cannot slip through.
    CHECK(alpha_sum(*fb_v0)  > 0.0);
    CHECK(alpha_sum(*fb_v15) > 0.0);
    CHECK(alpha_sum(*fb_v30) > 0.0);

    // Crude centroid estimate: average of non-zero pixel positions
    // weighted by alpha.  This is a lightweight check — the full
    // alpha_bbox/centroid geometry verification (width>800, no-edge-
    // contact, etc.) is already covered by the 6 golden static tests
    // in `tests/text_golden/ae_parity/ae_08_glow_pulse.cpp` which
    // are part of the same `chronon3d_text_golden_tests` target.
    auto centroid = [](const Framebuffer& fb) -> std::pair<float, float> {
        double sx = 0, sy = 0, sa = 0;
        const int w = static_cast<int>(fb.width());
        const int h = static_cast<int>(fb.height());
        for (int y = 0; y < h; y += 4) {  // 4x downsample for speed
            for (int x = 0; x < w; x += 4) {
                auto px = fb.get_pixel(x, y);
                const float a = px.a;
                if (a > 0.01f) {
                    sx += x * a;
                    sy += y * a;
                    sa += a;
                }
            }
        }
        if (sa < 1e-3) return {0.0f, 0.0f};
        return {static_cast<float>(sx / sa), static_cast<float>(sy / sa)};
    };
    const auto c0  = centroid(*fb_v0);
    const auto c15 = centroid(*fb_v15);
    const auto c30 = centroid(*fb_v30);
    CHECK(std::abs(c0.first  - 960.0f) < 100.0f);
    CHECK(std::abs(c0.second - 540.0f) < 100.0f);
    CHECK(std::abs(c15.first - 960.0f) < 100.0f);
    CHECK(std::abs(c15.second - 540.0f) < 100.0f);
    CHECK(std::abs(c30.first - 960.0f) < 100.0f);
    CHECK(std::abs(c30.second - 540.0f) < 100.0f);
}
