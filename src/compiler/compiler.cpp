#include <chronon3d/compiler/compiler.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/temporal_rate_analysis.hpp>
#include "compiler_stages.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace chronon3d::compiler {

namespace {

ProgramMetadata::OutputSpec lower_output(const render_plan::OutputSpec& output) {
    return {
        .path = output.path,
        .format = static_cast<std::uint8_t>(output.format),
        .codec = static_cast<std::uint8_t>(output.codec),
        .rate_control = static_cast<std::uint8_t>(output.rate_control),
        .bitrate = output.bitrate,
        .crf = output.crf,
        .qp = output.qp,
        .profile_id = output.profile_id,
    };
}

ProgramMetadata::RenderBudget lower_budget(const render_plan::RenderBudget& budget) {
    return {
        .max_width = budget.max_width,
        .max_height = budget.max_height,
        .max_total_pixels = budget.max_total_pixels,
        .max_frames = budget.max_frames,
        .max_layers = budget.max_layers,
        .max_text_bytes = budget.max_text_bytes,
        .max_asset_reference_bytes = budget.max_asset_reference_bytes,
        .max_estimated_output_bytes = budget.max_estimated_output_bytes,
        .max_peak_memory_bytes = budget.max_peak_memory_bytes,
        .max_temporal_pixels = budget.max_temporal_pixels,
    };
}

TemporalRate lower_temporal_rate(render_plan::TemporalRateClass rate) noexcept {
    switch (rate) {
        case render_plan::TemporalRateClass::Static: return TemporalRate::Static;
        case render_plan::TemporalRateClass::Cue: return TemporalRate::Cue;
        case render_plan::TemporalRateClass::KeyframeInterval:
            return TemporalRate::KeyframeInterval;
        case render_plan::TemporalRateClass::PerFrame: return TemporalRate::PerFrame;
        case render_plan::TemporalRateClass::Subframe: return TemporalRate::SubFrame;
    }
    return TemporalRate::PerFrame;
}

std::vector<std::int64_t> lower_schedule_frames(
    const std::vector<Frame>& frames) {
    std::vector<std::int64_t> lowered;
    lowered.reserve(frames.size());
    for (const auto frame : frames) lowered.push_back(frame.integral());
    return lowered;
}

TemporalUpdateSchedule build_temporal_update_schedule(
    const render_plan::RenderPlan& plan,
    const CompiledProgramData& data) {
    const auto report = render_plan::analyze_temporal_rates(plan);
    TemporalUpdateSchedule schedule;
    schedule.nodes.reserve(report.layers.size());
    const auto canvas_end = std::max<std::int64_t>(0, plan.canvas.duration.integral());
    for (std::size_t i = 0; i < report.layers.size(); ++i) {
        const auto& source = report.layers[i];
        TemporalNodeUpdate node;
        node.layer_index = static_cast<std::uint32_t>(i);
        node.active_first = 0;
        node.active_end = canvas_end;
        if (i < data.layers.size()) {
            const auto& layer = data.layers[i];
            node.active_first = std::clamp<std::int64_t>(layer.start_frame, 0, canvas_end);
            const auto end = layer.duration_frames > 0 &&
                    layer.start_frame > std::numeric_limits<std::int64_t>::max() -
                        layer.duration_frames
                ? std::numeric_limits<std::int64_t>::max()
                : layer.start_frame + layer.duration_frames;
            node.active_end = std::clamp<std::int64_t>(end, node.active_first, canvas_end);
        }
        node.rate = source.eval_frames_complete
            ? lower_temporal_rate(source.rate) : TemporalRate::PerFrame;
        if (node.rate == TemporalRate::Cue ||
            node.rate == TemporalRate::KeyframeInterval)
            node.frames = lower_schedule_frames(source.eval_frames);

        node.properties.reserve(source.properties.size());
        for (const auto& property : source.properties) {
            TemporalPropertyUpdate lowered;
            lowered.path = property.path;
            lowered.rate = property.eval_frames_complete
                ? lower_temporal_rate(property.rate) : TemporalRate::PerFrame;
            lowered.active_first = node.active_first;
            lowered.active_end = node.active_end;
            if (lowered.rate == TemporalRate::Cue ||
                lowered.rate == TemporalRate::KeyframeInterval)
                lowered.frames = lower_schedule_frames(property.eval_frames);
            node.properties.push_back(std::move(lowered));
        }
        schedule.nodes.push_back(std::move(node));
    }
    return schedule;
}

}  // namespace

std::vector<CompileError> validate_compiled_program(const CompiledProgramData&);

Result<CompiledProgram, CompileError> Compiler::compile(
    const RenderJobSource& source,
    assets::AssetResolver& resolver,
    const CompileOptions& options) const {
    return compile(source.plan, resolver, options);
}

Result<CompiledProgram, CompileError> Compiler::compile(
    const render_plan::RenderPlan& plan,
    assets::AssetResolver& resolver,
    const CompileOptions& options) const {
    auto asset_res = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    if (!asset_res) {
        return make_compile_error(
            CompileErrorCode::InvalidInput,
            "assets." + asset_res.error().logical_path,
            asset_res.error().message,
            "asset_resolver");
    }
    auto data = std::make_shared<CompiledProgramData>();
    data->metadata.schema = plan.schema;
    data->metadata.job_id = plan.job_id;
    data->metadata.width = static_cast<std::uint32_t>(plan.canvas.width);
    data->metadata.height = static_cast<std::uint32_t>(plan.canvas.height);
    data->metadata.duration_frames = plan.canvas.duration.integral();
    data->metadata.fps_numerator = plan.canvas.fps.numerator;
    data->metadata.fps_denominator = plan.canvas.fps.denominator;
    data->metadata.output = lower_output(plan.output);
    data->metadata.assets_root = resolver.mount_root().string();
    data->metadata.asset_manifest = std::move(asset_res).value();
    data->metadata.render_budget = lower_budget(plan.budget);
    data->metadata.lighting_rig = plan.lighting_rig;

    stages::lower_scene(plan, *data);
    data->temporal_schedule = build_temporal_update_schedule(plan, *data);
    stages::lower_animation(plan, *data);
    stages::lower_timeline(plan, *data);
    stages::lower_text(plan, *data);
    stages::build_dependencies(*data);
    stages::build_passes(*data);
    stages::optimize(*data);
    data->fingerprint_data.digest = stages::fingerprint(plan, *data);

    // Audit FP-1: same authority as the render-plan path — one digest pair for
    // one job, so a cache keyed on it cannot see two different values. The
    // canvas→settings defaulting that used to live here is part of the identity
    // and now lives in that authority too, so the two paths cannot diverge.
    const auto fp = render_plan::compute_render_job_fingerprint(
        plan, data->metadata.asset_manifest, options.fingerprint);
    data->metadata.content_digest = fp.content_digest;
    data->metadata.request_digest = fp.request_digest;
    data->fingerprint_data.content_digest = fp.content_digest;
    data->fingerprint_data.request_digest = fp.request_digest;

    const auto validation_errors = validate_compiled_program(*data);
    if (!validation_errors.empty()) return validation_errors.front();

    auto program = CompiledProgram{
        std::shared_ptr<const CompiledProgramData>(std::move(data))};
    return program;
}

}  // namespace chronon3d::compiler
