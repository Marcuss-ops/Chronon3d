#include <chronon3d/compiler/compiled_program.hpp>

#include <chronon3d/compiler/detail/compiled_program_plan_bridge.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <utility>

namespace chronon3d::compiler {

CompiledProgram::CompiledProgram(
    std::shared_ptr<const render_plan::PreparedRenderPlan> prepared_plan,
    std::shared_ptr<const CompiledProgramData> data)
    : prepared_plan_(std::move(prepared_plan)), data_(std::move(data)) {}

CompiledProgram::~CompiledProgram() = default;
CompiledProgram::CompiledProgram(CompiledProgram&&) noexcept = default;
CompiledProgram& CompiledProgram::operator=(CompiledProgram&&) noexcept = default;

const ProgramMetadata& CompiledProgram::metadata() const noexcept { return data_->metadata; }
const std::vector<CompiledNode>& CompiledProgram::nodes() const noexcept { return data_->nodes; }
const std::vector<CompiledPass>& CompiledProgram::passes() const noexcept { return data_->passes; }
const std::vector<ResourceBindingSpec>& CompiledProgram::resources() const noexcept { return data_->resources; }
const std::vector<AnimationProgram>& CompiledProgram::animations() const noexcept { return data_->animations; }
const std::vector<TimelineProgram>& CompiledProgram::timelines() const noexcept { return data_->timelines; }
const std::vector<AnimationTrackProgram>& CompiledProgram::animation_tracks() const noexcept { return data_->animation_tracks; }
const std::vector<CurveKeyframeProgram>& CompiledProgram::curve_keyframes() const noexcept { return data_->curve_keyframes; }
const std::vector<TextProgram>& CompiledProgram::texts() const noexcept { return data_->texts; }
const std::vector<std::string>& CompiledProgram::text_values() const noexcept { return data_->text_values; }
const std::vector<ParameterProgram>& CompiledProgram::parameters() const noexcept { return data_->parameters; }
const ProgramFingerprint& CompiledProgram::fingerprint_data() const noexcept { return data_->fingerprint_data; }

// ── Internal plan bridge (Wave 2 demolition seam) ─────────────────────────
// Defined here (and only here): the translation unit already holds the
// complete `render_plan::PreparedRenderPlan` definition, so the internal
// bridge header stays free of the heavyweight legacy execution types.

namespace detail {

const render_plan::PreparedRenderPlan&
CompiledProgramPlanBridge::prepared_plan(const CompiledProgram& program) noexcept {
    return *program.prepared_plan_;
}

const CompiledComposition&
CompiledProgramPlanBridge::compiled_composition(const CompiledProgram& program) noexcept {
    return program.prepared_plan_->compiled_composition;
}

}  // namespace detail
}  // namespace chronon3d::compiler
