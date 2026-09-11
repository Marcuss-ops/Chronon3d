#include <chronon3d/compiler/compiler.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include "compiler_stages.hpp"

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

    stages::lower_scene(plan, *data);
    stages::lower_animation(plan, *data);
    stages::lower_timeline(plan, *data);
    stages::lower_text(plan, *data);
    stages::build_dependencies(*data);
    stages::build_passes(*data);
    stages::optimize(*data);
    data->fingerprint_data.digest = stages::fingerprint(plan, *data);

    render_plan::RenderPlanFingerprintOptions fp_opts = options.fingerprint;
    if (fp_opts.render_settings.width == 0) fp_opts.render_settings.width = plan.canvas.width;
    if (fp_opts.render_settings.height == 0) fp_opts.render_settings.height = plan.canvas.height;
    const auto fp = stages::compute_digests(
        plan, data->metadata.asset_manifest, fp_opts);
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
