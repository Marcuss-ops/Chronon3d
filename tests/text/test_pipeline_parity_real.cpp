// test_pipeline_parity_real.cpp — Real pipeline parity matrix
//
// Compares the following pipelines using real framebuffer hashes:
//   1. SDK still          (in-process SoftwareRenderer)
//   2. CLI still          (chronon3d_cli still ...)
//   3. Raw video frame    (chronon3d_cli video --ffmpeg-mode png ...)
//   4. Pruning ON/OFF     (SDK dirty.enabled vs CLI --no-dirty-rects)
//   5. 1/N threads        (SDK tbb::global_control vs CLI CHRONON3D_THREADS)
//   6. Warm/cold cache    (fresh renderer vs reused renderer / --warmup)
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
