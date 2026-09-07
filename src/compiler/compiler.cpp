#include <chronon3d/compiler/compiler.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include "compiler_stages.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace chronon3d::compiler {

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
    auto prepared = render_plan::compile_render_plan(
        plan, resolver, options.fingerprint);
    if (!prepared) {
        auto error = std::move(prepared).error();
        return CompileError{CompileErrorCode::InvalidInput, std::move(error.path),
                            std::move(error.message), std::move(error.component)};
    }
    auto data = std::make_shared<CompiledProgramData>();
    data->metadata = {plan.schema, plan.job_id, static_cast<std::uint32_t>(plan.canvas.width),
                        static_cast<std::uint32_t>(plan.canvas.height), plan.canvas.duration.integral(),
                        plan.canvas.fps.numerator, plan.canvas.fps.denominator};
    stages::lower_scene(plan, *data);
    stages::lower_animation(plan, *data);
    stages::lower_timeline(plan, *data);
    stages::lower_text(plan, *data);
    stages::build_dependencies(*data);
    stages::build_passes(*data);
    stages::optimize(*data);
    data->fingerprint_data.digest = stages::fingerprint(plan, *data);
    const auto validation_errors = validate_compiled_program(*data);
    if (!validation_errors.empty()) return validation_errors.front();
    auto prepared_plan = std::make_shared<const render_plan::PreparedRenderPlan>(
        std::move(prepared).value());
    data->metadata.render_budget = prepared_plan->compiled_composition.render_budget;
    auto program = CompiledProgram{
        std::move(prepared_plan),
        std::shared_ptr<const CompiledProgramData>(std::move(data))};
    return program;
}

}  // namespace chronon3d::compiler
