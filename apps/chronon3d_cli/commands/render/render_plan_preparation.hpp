#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// render_plan_preparation.hpp — the single canonical render-plan preparation
// pipeline (M4).
//
// Every render-plan consumer goes through ONE method:
//
//   read/parse JSON → decode_render_plan → resolve assets → compile_render_plan
//
// `prepare_render_plan()` returns the decoded plan, the prepared (compiled)
// plan, the mounted resolver, the effective assets root and the effective
// render settings in one `PreparedRenderPlanContext`.  The CLI command layer
// is a thin adapter over this value:
//
//   RENDER      → context.prepared
//   VALIDATE    → context.prepared.fingerprint (via validate_render_plan)
//   INSPECT     → context.decoded / context.prepared (read-only view)
//
// There is no second dry-run/validate engine: `chronon validate --plan` and
// `chronon render --plan --dry-run` both call `validate_render_plan()`, which
// is `prepare_render_plan()` + one report emitter.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <string>

namespace chronon3d::cli {

struct RenderPlanPreparationOptions {
    std::string input;       // plan file path (used when `json` is empty)
    std::string json;        // inline JSON document (used when `input` is empty)
    std::string assets_root; // explicit; empty falls back to CHRONON3D_CLI_ASSETS_ROOT
};

struct PreparedRenderPlanContext {
    render_plan::RenderPlan decoded;
    render_plan::PreparedRenderPlan prepared;
    chronon3d::assets::AssetResolver resolver;
    std::string effective_assets_root;
    chronon3d::RenderSettings settings;
};

/// Canonical render-plan preparation.  Throws nothing: file-read and JSON
/// parse failures are mapped into `render_plan::PlanDecodeError`.
[[nodiscard]] Result<PreparedRenderPlanContext, render_plan::PlanDecodeError>
prepare_render_plan(const RenderPlanPreparationOptions& options);

/// Canonical render-plan validation gate.  Runs `prepare_render_plan()` and
/// emits a `{valid, status, ...}` JSON report to stdout.  Returns 0 on VALID,
/// 1 on INVALID.  Shared by `chronon validate --plan` and
/// `chronon render --plan --dry-run` (same canonical method).
int validate_render_plan(const RenderPlanPreparationOptions& options);

} // namespace chronon3d::cli
