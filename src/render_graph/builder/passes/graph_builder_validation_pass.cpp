#include "graph_builder_validation_pass.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/scene/validation/scene_validator.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chronon3d::graph::detail {

namespace {

// CONTRACT-VALIDATION-ENFORCEMENT — scene validation diagnostics used to be
// logged and then ignored ("we log but don't block"), which left severity
// Error findings without any enforcement point: an invalid scene proceeded
// to render and the failure surfaced later, far from its cause.
//
// Enforcement is OPT-IN via CHRONON3D_STRICT_SCENE_VALIDATION so existing
// producers that rely on the permissive build are not broken silently; when
// enabled, Severity::Error aborts the build with the same exception channel
// every other build pass uses (std::runtime_error — see
// graph_builder_source_pass.cpp). Warnings remain advisory in both modes.
[[nodiscard]] bool strict_scene_validation_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_STRICT_SCENE_VALIDATION");
        if (!value) return false;
        const std::string_view setting(value);
        return setting == "1" || setting == "true" || setting == "on";
    }();
    return enabled;
}

} // namespace

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

    if (strict_scene_validation_enabled()) {
        const auto errors = result.errors();
        if (!errors.empty()) {
            // Abort through the canonical pass-failure channel so the build
            // surfaces the first error at its point of detection.
            throw std::runtime_error(
                "[scene-validation] strict mode: " + std::to_string(errors.size()) +
                " scene validation error(s); first: " + errors.front().message +
                " (" + errors.front().context + ")");
        }
    }

    // Permissive default (strict mode off): log-only, as documented above.
    // Stricter enforcement can be extended via SceneValidationRegistry rules.
}

} // namespace chronon3d::graph::detail
