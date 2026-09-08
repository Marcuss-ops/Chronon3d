#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/compiler/compiled_pass.hpp>
#include <chronon3d/compiler/compiled_resource.hpp>
#include <chronon3d/compiler/compiled_layer.hpp>
#include <chronon3d/render_plan/render_budget.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
struct CompiledComposition;
namespace render_plan {
struct PreparedRenderPlan;
}
}

namespace chronon3d::compiler {

/// Internal plan bridge (Wave 2 demolition seam).  See
/// `<chronon3d/compiler/detail/compiled_program_plan_bridge.hpp>`.
/// Declared here so `CompiledProgram` can grant friendship; defined only in
/// the implementation (src/compiler/compiled_program.cpp).  Not part of the
/// SDK public surface and never installed.
namespace detail {
class CompiledProgramPlanBridge;
}  // namespace detail

/// Immutable result of the compiler boundary.
///
/// The concrete prepared plan is intentionally hidden behind a const shared
/// object and carries NO public accessor: compiler consumers stay
/// independent from execution implementation headers that are transitively
/// required by the legacy execution artifact.  The few execution seams that
/// still run on the legacy plan reach it exclusively through the internal
/// bridge above (demolition debt — see the bridge header's debt card).
struct ProgramMetadata {
    std::string schema;
    std::string job_id;
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::int64_t duration_frames{0};
    std::int32_t fps_numerator{0};
    std::int32_t fps_denominator{1};
    render_plan::RenderBudget render_budget{};
};

struct ProgramFingerprint {
    std::uint64_t digest{0};
    std::uint32_t algorithm_version{1};
};

/// Mutable assembly buffer used only between compiler passes.
struct CompiledProgramData {
    ProgramMetadata metadata;
    std::vector<CompiledNode> nodes;
    std::vector<StaticLayerProgram> layers;
    std::vector<CompiledPass> passes;
    std::vector<ResourceBindingSpec> resources;
    std::vector<AnimationProgram> animations;
    std::vector<TimelineProgram> timelines;
    std::vector<AnimationTrackProgram> animation_tracks;
    std::vector<CurveKeyframeProgram> curve_keyframes;
    std::vector<TextProgram> texts;
    std::vector<std::string> text_values;
    std::vector<ParameterProgram> parameters;
    ProgramFingerprint fingerprint_data;
};

/// Frozen, data-oriented compiler output. Frame-varying values belong to the
/// sampler/runtime, not to this object.
class CompiledProgram final {
public:
    ~CompiledProgram();
    CompiledProgram(CompiledProgram&&) noexcept;
    CompiledProgram& operator=(CompiledProgram&&) noexcept;
    CompiledProgram(const CompiledProgram&) = delete;
    CompiledProgram& operator=(const CompiledProgram&) = delete;

    [[nodiscard]] const ProgramMetadata& metadata() const noexcept;
    [[nodiscard]] const std::vector<CompiledNode>& nodes() const noexcept;
    [[nodiscard]] const std::vector<StaticLayerProgram>& layers() const noexcept;
    [[nodiscard]] const std::vector<CompiledPass>& passes() const noexcept;
    [[nodiscard]] const std::vector<ResourceBindingSpec>& resources() const noexcept;
    [[nodiscard]] const std::vector<AnimationProgram>& animations() const noexcept;
    [[nodiscard]] const std::vector<TimelineProgram>& timelines() const noexcept;
    [[nodiscard]] const std::vector<AnimationTrackProgram>& animation_tracks() const noexcept;
    [[nodiscard]] const std::vector<CurveKeyframeProgram>& curve_keyframes() const noexcept;
    [[nodiscard]] const std::vector<TextProgram>& texts() const noexcept;
    [[nodiscard]] const std::vector<std::string>& text_values() const noexcept;
    [[nodiscard]] const std::vector<ParameterProgram>& parameters() const noexcept;
    [[nodiscard]] const ProgramFingerprint& fingerprint_data() const noexcept;

    [[nodiscard]] std::uint64_t fingerprint() const noexcept { return data_->fingerprint_data.digest; }

private:
    explicit CompiledProgram(
        std::shared_ptr<const render_plan::PreparedRenderPlan> prepared_plan,
        std::shared_ptr<const CompiledProgramData> data);

    // Wave 2-9 migration debt: the legacy prepared plan is retained (and
    // owned for the program's lifetime) so the equivalence/execution seams
    // that still run on CompiledComposition keep working.  There is NO
    // public accessor — reach it only via detail::CompiledProgramPlanBridge
    // (see include/chronon3d/compiler/detail/compiled_program_plan_bridge.hpp).
    std::shared_ptr<const render_plan::PreparedRenderPlan> prepared_plan_;
    std::shared_ptr<const CompiledProgramData> data_;

    friend class Compiler;
    friend class detail::CompiledProgramPlanBridge;
};

}  // namespace chronon3d::compiler
