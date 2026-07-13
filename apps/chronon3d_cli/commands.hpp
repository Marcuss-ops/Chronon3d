#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include "utils/common/cli_utils.hpp"

namespace chronon3d {
namespace cli {

struct RenderQualityArgs {
    // PR1 — motion blur is now a tri-state (Off | TemporalAccumulation |
    // VelocityApproximation).  The legacy `motion_blur` boolean is kept as a
    // backward-compat shortcut: when set to true, mode is inferred to
    // TemporalAccumulation so callers that used `--motion-blur` pre-PR1 behave
    // identically.  New code should set `motion_blur_mode` explicitly.
    //   motion_blur_mode:  0=Off, 1=TemporalAccumulation, 2=VelocityApproximation
    //
    // Out-of-range ints are mapped to Off in cli_render_utils.hpp so the
    // renderer never sees an undefined enum value.
    bool   motion_blur{false};              // legacy shortcut
    int    motion_blur_mode{0};             // PR1: 0=Off,1=Temporal,2=Velocity
    int    motion_blur_samples{8};
    float  shutter_angle_deg{180.0f};
    float  shutter_phase_deg{-90.0f};
    int    motion_blur_pattern{1};          // 0=Uniform, 1=Stratified, 2=Halton
    int    motion_blur_filter{0};           // 0=Box, 1=Triangle, 2=Gaussian
    float  ssaa{1.0f};
};

struct RenderPipelineArgs {
    bool   use_modular_graph{true};
    bool   diagnostic{false};
    bool   diagnostic_plan{false};
    std::string diagnostic_plan_output;
    int    tile_size{0};
    RenderQualityArgs quality{};

    // Renderer warmup (preallocation + optional dummy frame)
    bool   no_dirty_rects{false};

    // Renderer warmup (preallocation + optional dummy frame)
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{2};
    bool   warmup_dummy_frame{false};

    // Debug: force scalar (non-SIMD) Normal blend for diagnosing regression
    bool   force_scalar_normal_blend{false};

    // Framebuffer pool retention budget (MB). 0 = use default / env var.
    size_t fb_pool_budget_mb{0};

    // SceneProgramCache capacity per Precomp node. 0 = use default (8).
    size_t program_cache_capacity{0};

    // Auto-tune SceneProgramCache capacity based on hit/eviction ratio.
    bool program_cache_tune{false};
    // How many frames between auto-tune checks (default 30).
    size_t program_cache_tune_interval{30};
    // Minimum capacity when auto-tuning down (default 2).
    size_t program_cache_tune_min_capacity{2};
    // Maximum capacity when auto-tuning up (default 128).
    size_t program_cache_tune_max_capacity{128};

    // Text layout debug overlay + structured log per TextRun.
    bool text_layout_debug{false};

    // Diagnostic overlay on text layers (bbox, anchor, baseline).
    bool diagnostic_overlay{false};
    // Diagnostic overlay only (transparent background, no scene content).
    bool diagnostic_overlay_only{false};

    // Text layout debug JSON export path.
    // When non-empty and text_layout_debug is true, writes a JSON file
    // with per-TextRun bounds data (alpha_bounds, layout_bounds,
    // scratch_bounds, ascent, descent).  Append mode (JSONL).
    std::string text_layout_debug_json_path;
};

struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"}; // Supports "0", "0-90", "0-90x5"
    std::string output;      // No default
    RenderPipelineArgs pipeline{};
    std::string log_level{"info"};
    bool benchmark_all{false};
    bool report{false};
    std::string command_line; // reconstructed from argv
    chronon3d::CpuBudget cpu_budget;

    // Quick-preview batch count: when > 0, dev preview renders frames 0..count-1
    // as sequential PNGs instead of a single frame.  This avoids the full render
    // or video-encode path, giving a sub-second feedback loop for timing checks.
    int quick_frames{0};
};

struct VideoArgs {
    std::string comp_id;
    std::string output;
    Frame start{0};
    Frame end{0};
    int fps{30};
    int crf{16};                       // Production-clean: 16 is the sweet spot for text/glow at 1080p
    std::string codec{"auto"};
    std::string encode_preset{"slow"}; // Production-clean: 'medium' for prototypes, 'slow' for finals
    std::string tune;
    bool keep_frames{false};
    RenderPipelineArgs pipeline{};
    std::string frames_dir;
    std::string hardware_encoder{"none"};
    int chunks{1};

    std::string ffmpeg_mode{"pipe"};
    bool ffmpeg_verbose{false};
    std::string pipe_pixfmt{"rgba"};
    std::string color_output{"srgb"};
    std::string pipe_writer{"classic"};
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    std::string encoder_backend{"native"};
#else
    std::string encoder_backend{"pipe"};
#endif

    // Sink type selection: "ffmpeg", "null-render", "null-convert"
    std::string sink_type{"ffmpeg"};

    bool dry_run{false};
    chronon3d::CpuBudget cpu_budget;
};

struct VideoCameraArgs {
    std::string axis{"Tilt"};
    std::string reference_image{"assets/images/camera_reference.jpg"};
    std::string output;
    Frame start{0};
    Frame end{60};
    float roll_start_deg{-4.0f};
    float roll_end_deg{0.0f};
    int width{1280};
    int height{720};
    int fps{30};
    int crf{18};
    std::string codec{"auto"};
    std::string encode_preset{"medium"};
    std::string tune;
    RenderPipelineArgs pipeline{};
    std::string hardware_encoder{"none"};
};


struct BenchConvertArgs {
    std::string comp_id;
    int frame{0};
    int iterations{10};
    std::string format{"yuv420p"};
    bool apply_gamma{true};
};

struct BenchArgs {
    std::string comp_id;
    int frames{100};
    int warmup{10};
    bool use_modular_graph{true};
    bool no_dirty_rects{false};

    std::string json_file;
    std::string compare_file;

    bool quiet{false};
    bool include_frame_times{false};
    double fail_if_avg_slower_pct{0.0};

    // Renderer warmup (preallocation + optional dummy frame)
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{2};
    bool   warmup_dummy_frame{false};
};

struct GraphArgs {
    std::string comp_id;
    Frame frame{0};
    std::string output;   // optional .dot output path
    bool summary{false};  // print node/cache/timing diagnostics
    bool plan{false};     // print layer/frame placement before execution
};

struct TelemetryArgs {
    std::string run_id;
    std::string output_file;
};

struct PreflightArgs {
    std::string comp_id;
    Frame start{0};
    Frame end{0};
    int sample_step{1};
    std::string output;
    std::string json_file;
    bool legacy_preflight{false};  // FIX 8: legacy RenderPreflight behind flag
};

struct StillArgs {
    std::string comp_id;
    Frame frame{0};
    std::string output;
    RenderPipelineArgs pipeline{};
    std::string log_level{"info"};
    bool skip_preflight{false};
};

struct BakeLayerArgs {
    std::string comp_id;
    std::string layer_id;
    int frame{0};
    std::string output;
    bool quiet{false};
    bool diagnostic{false};
    bool diagnostic_overlay{false};
    bool diagnostic_overlay_only{false};
    bool exr_bake{false};
};

struct CameraPathArgs {
    std::string comp_id;
    Frame start{0};
    Frame end{0};
    int step{1};
    std::string output;          // output file path (auto-detects json/csv from extension)
    std::string format{"auto"}; // "json", "csv", or "auto" (detect from -o extension)
};

struct TextAuditArgs {
    std::string comp_id;
    std::string frames;            // e.g. "0,19,20,40,80,160,299" or "0-300x10"
    std::string json_output;       // --json output path
    std::string render_dir;        // --render-dir for frame PNGs
    float safe_margin_x{0.05f};    // fraction of canvas width
    float safe_margin_y{0.05f};    // fraction of canvas height
    float max_center_error_px{2.0f};
    int   max_border_alpha_pixels{0};
    float glyph_tolerance{0.01f};
    int   alpha_threshold{8};
    bool  diagnostic_overlay{false};
    bool  diagnostic_overlay_only{false};
};

// §12 FU09 — TICKET-SIMPLICITY-INSPECT-TEXT: per-node TextRun audit
// with structured JSON output + exit code (0=PASS, 1=FAIL, 2=VIOLATION).
struct InspectTextArgs {
    std::string comp_id;          // Composition name (required)
    Frame       frame{0};          // --frame N (required)
    bool        json{true};        // --json: emit JSON to stdout (default on)
};

// TICKET-CHRONON-GLOW-FINAL Fase 5 — `chronon3d_cli text-def-inspect`:
// after the render, consume `renderer.text_audit_snapshots()` and call
// `audit_text_visibility()` per snapshot.  Output: JSON array of
// per-snapshot objects (node/font_resolved/glyph_count/
// predicted_contains_world/alpha_bbox_empty/status).  Gated by
// `CHRONON3D_BUILD_DIAGNOSTICS` — in non-diagnostic builds the command
// emits an error JSON and returns exit 1.
struct TextDefInspectArgs {
    std::string comp_id;          // Composition name (required)
    Frame       frame{0};          // --frame N (default 0)
    std::string json_output;      // --json-output path (empty = stdout)
};

int command_list(const CompositionRegistry& registry);
int command_watch(const CompositionRegistry& registry, const std::string& comp_id);
int command_daemon(const CompositionRegistry& registry,
                   const std::string& assets_root = "",
                   const std::string& build_command = "");
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_doctor(const CompositionRegistry& registry);
int command_verify(const CompositionRegistry& registry, const std::string& output_dir);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);
int command_video(const CompositionRegistry& registry, const VideoArgs& args);
int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args);
int command_bench_convert(const CompositionRegistry& registry, const BenchConvertArgs& args);
int command_bench(const CompositionRegistry& registry, const BenchArgs& args);
int command_graph(const CompositionRegistry& registry, const GraphArgs& args);
int command_batch(const CompositionRegistry& registry, const std::vector<std::string>& job_specs);
int command_telemetry(const TelemetryArgs& args);
int command_preflight(const CompositionRegistry& registry, const PreflightArgs& args, AssetRegistry& assets);
int command_still(const CompositionRegistry& registry, const StillArgs& args);
int command_bake_layer(const CompositionRegistry& registry, const BakeLayerArgs& args);
int command_camera_path(const CompositionRegistry& registry, const CameraPathArgs& args);
int command_text_audit(const CompositionRegistry& registry, const TextAuditArgs& args);
int command_inspect_text(const CompositionRegistry& registry, const InspectTextArgs& args);
int command_text_def_inspect(const CompositionRegistry& registry, const TextDefInspectArgs& args);

} // namespace cli
} // namespace chronon3d
