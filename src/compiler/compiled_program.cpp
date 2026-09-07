#include <chronon3d/compiler/compiled_program.hpp>

#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <utility>

namespace chronon3d::compiler {

CompiledProgram::CompiledProgram(
    std::shared_ptr<const render_plan::PreparedRenderPlan> prepared_plan)
    : prepared_plan_(std::move(prepared_plan)) {}

CompiledProgram::~CompiledProgram() = default;
CompiledProgram::CompiledProgram(CompiledProgram&&) noexcept = default;
CompiledProgram& CompiledProgram::operator=(CompiledProgram&&) noexcept = default;

const render_plan::PreparedRenderPlan& CompiledProgram::prepared_plan() const noexcept { return *prepared_plan_; }
const CompiledComposition& CompiledProgram::compiled_composition() const noexcept { return prepared_plan_->compiled_composition; }
const render_plan::CanvasSpec& CompiledProgram::canvas() const noexcept { return prepared_plan_->canvas; }
const render_plan::OutputSpec& CompiledProgram::output() const noexcept { return prepared_plan_->output; }
const std::string& CompiledProgram::job_id() const noexcept { return prepared_plan_->job_id; }

}  // namespace chronon3d::compiler
