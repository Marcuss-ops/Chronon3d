#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/compiler/compiled_pass.hpp>
#include <chronon3d/compiler/compiled_resource.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
struct CompiledComposition;
namespace render_plan {
struct CanvasSpec;
struct OutputSpec;
struct PreparedRenderPlan;
}
}

namespace chronon3d::compiler {

/// Immutable result of the compiler boundary.
///
/// The concrete prepared plan is intentionally hidden behind a const shared
/// object. This keeps compiler consumers independent from execution
/// implementation headers that are transitively required by the legacy
/// execution artifact.
struct ProgramMetadata {
    std::string schema;
    std::string job_id;
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::int64_t duration_frames{0};
    std::int32_t fps_numerator{0};
    std::int32_t fps_denominator{1};
};

struct ProgramFingerprint {
    std::uint64_t digest{0};
    std::uint32_t algorithm_version{1};
};

/// Immutable, data-oriented compiler output. The legacy execution artifact
/// below is a temporary bridge and is not part of the IR graph.
class CompiledProgram final {
public:
    ProgramMetadata metadata;
    std::vector<CompiledNode> nodes;
    std::vector<CompiledPass> passes;
    std::vector<ResourceBindingSpec> resources;
    std::vector<AnimationProgram> animations;
    ProgramFingerprint fingerprint_data;

    ~CompiledProgram();
    CompiledProgram(CompiledProgram&&) noexcept;
    CompiledProgram& operator=(CompiledProgram&&) noexcept;
    CompiledProgram(const CompiledProgram&) = delete;
    CompiledProgram& operator=(const CompiledProgram&) = delete;

    [[nodiscard]] const render_plan::PreparedRenderPlan& prepared_plan() const noexcept;
    [[nodiscard]] const CompiledComposition& compiled_composition() const noexcept;
    [[nodiscard]] const render_plan::CanvasSpec& canvas() const noexcept;
    [[nodiscard]] const render_plan::OutputSpec& output() const noexcept;
    [[nodiscard]] const std::string& job_id() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept { return fingerprint_data.digest; }

private:
    explicit CompiledProgram(
        std::shared_ptr<const render_plan::PreparedRenderPlan> prepared_plan);

    std::shared_ptr<const render_plan::PreparedRenderPlan> prepared_plan_;

    friend class Compiler;
};

}  // namespace chronon3d::compiler
