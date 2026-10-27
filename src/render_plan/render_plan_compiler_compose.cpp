#include "render_plan_compiler_entry_detail.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

#include <stdexcept>

namespace chronon3d::render_plan::compiler_entry_detail {

std::optional<PlanDecodeError> compile_composition_view(
    const CompositionDefinition& definition,
    const compiler::CompiledProgram* canonical_program,
    CompiledComposition& compiled_value,
    std::shared_ptr<const Composition>& composition,
    double& elapsed_ms) {
    const auto composition_t0 = chronon3d::profiling::now();
    if (canonical_program != nullptr) {
        // The CLI owns the canonical CompiledProgram. Its video executor only
        // needs a scene-evaluation compatibility object, so avoid compiling
        // the same composition a second time here.
        composition = std::make_shared<const Composition>(
            definition.composition, definition.scene,
            definition.scene_content_fingerprint);
        compiled_value.composition = composition;
        compiled_value.fingerprint = canonical_program->fingerprint();
    } else {
        // Standalone callers of the prepared-plan API retain the historical
        // composition compiler behavior.
        Composition definition_composition(
            definition.composition, definition.scene,
            definition.scene_content_fingerprint);
        auto result = chronon3d::compile_composition(definition_composition, {});
        if (!result)
            return PlanDecodeError{"composition", result.error().message};

        compiled_value = std::move(result).value();
        auto compiled_view =
            std::make_shared<const CompiledComposition>(compiled_value);
        composition = std::make_shared<Composition>(
            definition.composition,
            [compiled_view](const FrameContext& context) {
                auto evaluated = chronon3d::evaluate(
                    *compiled_view,
                    CompositionEvaluateContext{.frame_context = context},
                    context.local_time());
                if (!evaluated) {
                    throw std::runtime_error(
                        "compiled render-plan evaluation failed: " +
                        evaluated.error().message);
                }
                return std::move(evaluated.value().scene);
            },
            definition.scene_content_fingerprint);
    }
    elapsed_ms = chronon3d::profiling::duration_ms(
        composition_t0, chronon3d::profiling::now());
    return std::nullopt;
}

} // namespace chronon3d::render_plan::compiler_entry_detail
