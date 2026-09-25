#include <chronon3d/compiler/compiled_program.hpp>

#include <utility>

namespace chronon3d::compiler {

CompiledProgram::CompiledProgram(
    std::shared_ptr<const CompiledProgramData> data)
    : data_(std::move(data)) {}

CompiledProgram::~CompiledProgram() = default;
CompiledProgram::CompiledProgram(CompiledProgram&&) noexcept = default;
CompiledProgram& CompiledProgram::operator=(CompiledProgram&&) noexcept = default;

const ProgramMetadata& CompiledProgram::metadata() const noexcept { return data_->metadata; }
const std::vector<CompiledNode>& CompiledProgram::nodes() const noexcept { return data_->nodes; }
const std::vector<StaticLayerProgram>& CompiledProgram::layers() const noexcept { return data_->layers; }
const std::vector<std::uint32_t>& CompiledProgram::layer_evaluation_order() const noexcept { return data_->layer_evaluation_order; }
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

}  // namespace chronon3d::compiler
