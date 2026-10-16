#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// render_plan_preparation.hpp — the single canonical render-plan preparation
// pipeline (M4).
//
// Every render-plan consumer goes through ONE method:
//
//   read/parse JSON → decode_render_plan → resolve assets → compiler::compile
//
// `prepare_render_plan()` returns the decoded plan, the prepared (compiled)
// plan, the mounted resolver, the effective assets root and the effective
// render settings in one `PreparedRenderPlanContext`.  The CLI command layer
// is a thin adapter over this value:
//
//   RENDER      → context.program (CompiledProgram)
//   VALIDATE    → context.program->fingerprint_data()
//   INSPECT     → context.decoded / context.program->metadata()
//
// Wave 2 (Pure CompiledProgram): `CompiledProgram` is a pure typed IR without
// legacy plan attachments. Code consumes the numeric program
// (metadata + nodes/passes).
//
// There is no second dry-run/validate engine: `chronon validate --plan` and
// `chronon render --plan --dry-run` both call `validate_render_plan()`, which
// is `prepare_render_plan()` + one report emitter.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct RenderPlanPreparationOptions {
    std::string input;       // plan file path (used when `json` is empty)
    std::string json;        // inline JSON document (used when `input` is empty)
    std::string assets_root; // explicit; empty falls back to CHRONON3D_CLI_ASSETS_ROOT
    std::string prepared_package; // optional immutable overlay preparation sidecar
    // True when the sink selects the video lane (e.g. --video-sink raw) even for
    // still/PNG plan outputs; that lane requires the compatibility view.
    bool require_video_compatibility_view = false;
};

struct PreparedOverlayPackageSummary {
    struct OverlayBinding {
        std::string id;
        std::string kind;
        std::string text_key;
        std::string asset_key;
    };
    struct TextBinding {
        std::string key;
        std::string text;
    };
    struct AssetBinding {
        std::string key;
        std::string logical_path;
        std::string content_hash;
    };

    std::string path;
    std::string plan_id;
    std::string language;
    std::string content_hash;
    std::size_t asset_count{0};
    std::size_t text_run_count{0};
    std::size_t overlay_count{0};
    std::vector<OverlayBinding> overlays;
    std::vector<TextBinding> text_runs;
    std::vector<AssetBinding> assets;
};

struct PreparedRenderPlanContext {
    render_plan::RenderPlan decoded;
    std::optional<PreparedOverlayPackageSummary> prepared_package;
    std::shared_ptr<const compiler::CompiledProgram> program;
    // Video export still consumes the composition-shaped executor until its
    // encoder bridge is numeric-program native. This is a temporary view
    // materialized from `program`; it is not a second compiler result.
    std::shared_ptr<const CompiledComposition> video_compatibility_view;
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
