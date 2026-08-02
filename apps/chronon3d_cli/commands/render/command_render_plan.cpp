#include "../../command_registry.hpp"
#include "../../cli_context.hpp"
#include "../../utils/job/render_job.hpp"
#include "audio_muxer.hpp"
#include "command_render_plan.hpp"

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace chronon3d::cli {
namespace {

struct RenderPlanState {
    std::string input;
    std::string output;
    std::string assets_root;
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
    std::uint32_t fps_num{30};
    std::uint32_t fps_den{1};
};

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open render plan: " + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string codec_name(render_plan::VideoCodec codec) {
    switch (codec) {
        case render_plan::VideoCodec::H264: return "h264";
        case render_plan::VideoCodec::H265: return "h265";
        case render_plan::VideoCodec::VP9: return "vp9";
        case render_plan::VideoCodec::AV1: return "av1";
        case render_plan::VideoCodec::Auto: return "auto";
    }
    return "auto";
}

bool video_output(const std::string& path) {
    const auto extension = std::filesystem::path(path).extension().string();
    return extension == ".mp4" || extension == ".mkv" || extension == ".webm" ||
           extension == ".mov";
}

int execute_render_plan(CliContext& ctx, const RenderPlanState& args) {
    try {
        const auto root = nlohmann::json::parse(read_file(args.input));
        const auto decoded = render_plan::decode_render_plan(root);
        if (!decoded) {
            spdlog::error("Render plan decode failed: {}", decoded.error().message);
            return 1;
        }
        const std::string effective_assets_root = !args.assets_root.empty()
            ? args.assets_root
            : (std::getenv("CHRONON3D_CLI_ASSETS_ROOT")
                ? std::getenv("CHRONON3D_CLI_ASSETS_ROOT") : "");
        auto plan = decoded.value();
        chronon3d::assets::AssetResolver resolver;
        if (!effective_assets_root.empty()) {
            resolver.mount(std::filesystem::path{effective_assets_root});
        }
        RenderSettings effective_settings;
        effective_settings.fail_on_missing_assets = true;
        render_plan::RenderPlanFingerprintOptions fingerprint_options;
        fingerprint_options.render_settings.width = plan.canvas.width;
        fingerprint_options.render_settings.height = plan.canvas.height;
        fingerprint_options.render_settings.ssaa_factor = effective_settings.ssaa_factor;
        fingerprint_options.render_settings.motion_blur =
            chronon3d::is_motion_blur_active(effective_settings.motion_blur);
        fingerprint_options.render_settings.dirty_rects = effective_settings.dirty.enabled;
        fingerprint_options.render_settings.dirty_bitmask = effective_settings.dirty.use_bitmask;
        fingerprint_options.render_settings.dirty_tiles = effective_settings.dirty.use_tiles;
        fingerprint_options.render_settings.parallel_tiles = effective_settings.dirty.parallel_tiles;
        fingerprint_options.render_settings.tile_size = effective_settings.dirty.tile_size;
        fingerprint_options.render_settings.tile_dirty_ratio_threshold =
            effective_settings.dirty.tile_dirty_ratio_threshold;
        fingerprint_options.render_settings.optimize_compositing =
            effective_settings.compositing.optimize_compositing;
        fingerprint_options.render_settings.deterministic =
            effective_settings.force_scalar_normal_blend;
        fingerprint_options.render_settings.force_scalar_normal_blend =
            effective_settings.force_scalar_normal_blend;
        const auto compiled = render_plan::compile_render_plan(
            plan, resolver, fingerprint_options);
        if (!compiled) {
            spdlog::error("Render plan compilation failed: {}", compiled.error().message);
            return 1;
        }

        const auto output = args.output.empty() ? decoded->output.path : args.output;
        const auto first = static_cast<std::int64_t>(args.start_frame);
        const auto last = args.end_frame == 0
            ? decoded->canvas.duration.integral() - 1
            : static_cast<std::int64_t>(args.end_frame);
        RenderRequest request;
        const auto& prepared = compiled.value();
        request.comp_id = prepared.job_id;
        request.prepared_comp = prepared.composition;
        request.output = output;
        request.execution.assets_root = effective_assets_root.empty()
            ? std::nullopt
            : std::optional<std::filesystem::path>{effective_assets_root};
        request.settings = effective_settings;
        request.video_settings.fps = args.fps_num == 30 && args.fps_den == 1
            ? decoded->canvas.fps : static_cast<int>(args.fps_num / args.fps_den);
        request.video_settings.codec = codec_name(decoded->output.codec);
        if (video_output(output)) {
            request.mode = RenderMode::Video;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
            request.execution.warmup_renderer = true;
            request.execution.warmup_dummy_frame = true;
        } else if (first == last) {
            request.mode = RenderMode::Still;
            request.still_frame = Frame{first};
        } else {
            request.mode = RenderMode::Sequence;
            request.first_frame = Frame{first};
            request.last_frame = Frame{last};
        }

        auto job = resolve_render_request(ctx.registry, std::move(request));
        if (!job) {
            spdlog::error("Render plan job failed: {}", job.error().message);
            return 1;
        }
        auto result = execute_render_job(*job);
        if (!result) {
            spdlog::error("Render plan job failed: {}", result.error().message);
            return 1;
        }
        if (video_output(output) && !AudioMuxer{}.mux(output, prepared.audio_tracks,
                                                       resolver))
            return 1;
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Render plan error: {}", error.what());
        return 1;
    }
}

}  // namespace

int run_render_plan_file(CliContext& ctx,
                         const std::string& input,
                         const std::string& output,
                         const std::string& assets_root) {
    RenderPlanState state;
    state.input = input;
    state.output = output;
    state.assets_root = assets_root;
    return execute_render_plan(ctx, state);
}

void register_render_plan_command(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "render-plan", "Compatibility alias for render --plan");
    auto state = std::make_shared<RenderPlanState>();
    command->add_option("--input", state->input, "Render plan JSON")->required();
    command->add_option("--output", state->output, "Output file (overrides plan output.path)");
    command->add_option("--assets-root", state->assets_root, "Instance asset root");
    command->add_option("--start-frame", state->start_frame, "First frame");
    command->add_option("--end-frame", state->end_frame, "Last frame, inclusive");
    command->add_option("--fps-num", state->fps_num, "Frame-rate numerator");
    command->add_option("--fps-den", state->fps_den, "Frame-rate denominator");
    command->callback([state, &ctx] { ctx.exit_code = execute_render_plan(ctx, *state); });
}

}  // namespace chronon3d::cli
