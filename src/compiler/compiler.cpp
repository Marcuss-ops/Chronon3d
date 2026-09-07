#include <chronon3d/compiler/compiler.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include "compiler_stages.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace chronon3d::compiler {

std::vector<CompileError> validate_compiled_program(const CompiledProgram&);

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
    auto program = CompiledProgram{
        std::make_shared<const render_plan::PreparedRenderPlan>(std::move(prepared).value())};
    program.metadata = {plan.schema, plan.job_id, static_cast<std::uint32_t>(plan.canvas.width),
                        static_cast<std::uint32_t>(plan.canvas.height), plan.canvas.duration.integral(),
                        plan.canvas.fps.numerator, plan.canvas.fps.denominator};
    stages::lower_scene(plan, program);
    stages::lower_animation(plan, program);
    stages::lower_timeline(plan, program);
    stages::lower_text(plan, program);
    stages::build_dependencies(program);
    stages::build_passes(program);
    stages::optimize(program);
    program.fingerprint_data.value = stages::fingerprint(plan, program);
    const auto validation_errors = validate_compiled_program(program);
    if (!validation_errors.empty()) return validation_errors.front();
    return program;
}

}  // namespace chronon3d::compiler
