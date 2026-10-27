#pragma once

#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <memory>

namespace chronon3d::render_plan::compiler_entry_detail {

[[nodiscard]] std::optional<PlanDecodeError> validate_compile_request(
    const RenderPlan& plan,
    const RuntimeCameraSampleProvider* runtime_camera);

[[nodiscard]] std::optional<PlanDecodeError> compile_composition_view(
    const CompositionDefinition& definition,
    const compiler::CompiledProgram* canonical_program,
    CompiledComposition& compiled,
    std::shared_ptr<const Composition>& composition,
    double& elapsed_ms);

} // namespace chronon3d::render_plan::compiler_entry_detail
