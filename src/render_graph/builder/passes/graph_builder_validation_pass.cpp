#include "graph_builder_validation_pass.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/scene/validation/scene_validator.hpp>
#include <chronon3d/scene/scene.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

void ValidationPass::run(GraphBuildContext& ctx) {
    if (!ctx.scene) return;

    SceneValidator validator;
    auto result = validator.validate(*ctx.scene);

    for (const auto& diag : result.diagnostics) {
        switch (diag.severity) {
            case ValidationSeverity::Error:
                spdlog::error("[scene-validation] {} ({})", diag.message, diag.context);
                break;
            case ValidationSeverity::Warning:
                spdlog::warn("[scene-validation] {} ({})", diag.message, diag.context);
                break;
            case ValidationSeverity::Info:
                spdlog::info("[scene-validation] {} ({})", diag.message, diag.context);
                break;
        }
    }

    // Note: we log but don't block on errors — the graph builder may still
    // produce useful output (e.g. layers without the invalid parent still render).
    // Stricter enforcement can be added later via SceneValidationRegistry rules.
}

} // namespace chronon3d::graph::detail
