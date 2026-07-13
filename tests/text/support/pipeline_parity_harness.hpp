// ============================================================================
// tests/text/support/pipeline_parity_harness.hpp
//
// TICKET-REFACTOR-TESTS-SPLIT-18-19 §A — pipeline parity test harness.
//
// Extracted from the 717-LoC test_pipeline_parity_real.cpp (now split into
// 4 dedicated test files: still / video / runtime_modes / chronon_glow_temporal).
// Provides the shared helper API used by all 4 test files:
//
//   - CLI path discovery (CHRONON3D_CLI_PATH macro / build/ fallback)
//   - Per-test temporary directory creation (PID + counter unique)
//   - Subprocess execution via std::system
//   - FFmpeg availability probe
//   - PNG → Framebuffer → u64 hash pipeline (regression-locks the rendering)
//   - SDK render-to-PNG helper (with optional warm-up render)
//   - CLI still subprocess (chronon3d_cli still PipelineParityCanary ...)
//   - CLI video first-frame subprocess (chronon3d_cli video ... --ffmpeg-mode png ...)
//   - CLI video full-sequence render for ChrononGlowFinalAE
//   - Per-frame alpha bbox + centroid + max_alpha metrics (spec §5)
//   - Default RenderSettings factory
//
// Cat-3 minimal-surface: 1 namespace, 0 new SDK symbols.  All helpers
// previously lived in an anonymous namespace inside the monolithic test
// file; the split simply promotes them to a shared header.
//
// Asset mounting is a no-op here (the original tests do NOT mount assets —
// the canary composition uses only Inter-Bold.ttf which is resolved at the
// project root by the canonical AssetResolver).  The forward-point to add
// explicit `runtime.resolver().mount(work_dir)` calls is in
// TICKET-PARITY-HARNESS-ASSET-MOUNT-EXPLICIT.
// ============================================================================

#pragma once

#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

namespace chronon3d::test::parity_harness {

// CLI binary path (CHRONON3D_CLI_PATH macro from CMakeLists → default
// build/chronon3d_cli).
std::string get_cli_path();

// Create a unique per-test temp dir (PID + counter) under std::temp_directory_path.
std::filesystem::path make_temp_dir();

// Run a shell command via std::system; returns the exit code.
int run_command(const std::string& cmd);

// True iff `ffmpeg -version` exits 0 in the current environment.
bool ffmpeg_available();

// Load a PNG into a Framebuffer and return the u64 hash.  REQUIREs non-null.
std::uint64_t hash_from_png(const std::string& path);

// Render PipelineParityCanary with the given settings (optionally with a
// warm-up render to prime caches) and save the PNG at png_path.
// Returns the u64 hash of the saved PNG.
std::uint64_t render_sdk_to_png(const std::string& png_path,
                                const chronon3d::RenderSettings& settings,
                                bool warm = false);

// Run `chronon3d_cli still PipelineParityCanary --skip-preflight ...` and
// save the output at out_png.  Returns true on exit-0.
bool run_cli_still(const std::string& out_png,
                   const std::string& extra_args,
                   const std::string& env_prefix = {});

// Run `chronon3d_cli video PipelineParityCanary ... --ffmpeg-mode png
// --keep-frames --frames-dir <work_dir>/frames` and return the absolute
// path of the extracted first frame.  Empty path on failure.
std::string run_cli_video_first_frame(const std::string& work_dir,
                                      const std::string& extra_args);

// CLI video full-sequence render for ChrononGlowFinalAE — returns the
// .mp4 path + frames dir.  Empty paths on CLI failure.
struct VideoRenderResult {
    std::string video_path;       // path to the .mp4
    std::string frames_dir;       // directory containing per-frame PNGs
};
VideoRenderResult run_cli_video_chronon_glow_final(
    const std::string& work_dir,
    int start_frame,
    int end_frame);

// Per-frame alpha bbox + centroid + max_alpha metrics (spec §5).
// No downsample (unlike the legacy 4x-downsample lambda in the Fase 6
// test); each pixel contributes to bbox + centroid + max_alpha per
// user-spec verbatim "a > 3 pixels".
struct FrameMetrics {
    int frame = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;          // alpha bbox (a > 3/255)
    int width = 0, height = 0;                    // bbox width / height
    double centroid_x = 0.0, centroid_y = 0.0;    // alpha-weighted centroid
    int visible_pixels = 0;                       // count of a > 3/255
    double alpha_sum = 0.0;                       // sum of a in [0, 1]
    int max_alpha = 0;                            // max a in [0, 255]
};
FrameMetrics compute_frame_metrics(const chronon3d::Framebuffer& fb, int frame_num);

// Default RenderSettings (modular graph ON, dirty-rect pruning defaults).
chronon3d::RenderSettings default_settings();

} // namespace chronon3d::test::parity_harness
