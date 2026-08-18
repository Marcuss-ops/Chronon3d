#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// render_plan_inspection.hpp — M5: read-only diagnostic view of a prepared
// render plan.
//
// `ResolvedRenderPlanInspection` is DERIVED from `PreparedRenderPlanContext`
// (the same canonical value produced by `prepare_render_plan()` and consumed
// by render/validate).  It is NOT an input to the renderer: the renderer
// keeps consuming `PreparedRenderPlan::compiled_composition`.  This view
// exists only for `chronon inspect --plan` (human + JSON) and future test
// introspection.
//
// No field here re-runs preset materialisation or anchor resolution: those
// belong to the renderer's single resolution path.  The inspection reports
// the REQUESTED plan values plus the deterministic facts already computed by
// prepare_render_plan() (fingerprints and the resolved asset manifest).
// ─────────────────────────────────────────────────────────────────────────────

#include "render_plan_preparation.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct JobInspection {
    std::string id;
    std::string schema;
    std::string content_digest;
    std::string request_digest;
    std::string asset_manifest_digest;
};

struct CanvasInspection {
    int width{0};
    int height{0};
    int fps{0};
    int frames{0};
};

struct FontInspection {
    std::string asset;
    std::string family;
    std::optional<int> weight;
    bool resolved{false};
};

struct PresetInspection {
    std::string requested;
};

struct LayoutInspection {
    std::string requested_anchor;
    std::string alignment;
    std::optional<float> x;
    std::optional<float> y;
    std::optional<float> width;
    std::optional<float> height;
    std::optional<float> offset_x;
    std::optional<float> offset_y;
};

struct MotionInspection {
    std::string preset;
    std::string unit;
    std::optional<int> enter_frames;
    std::optional<int> exit_frames;
};

struct ResolvedLayerInspection {
    std::string id;
    std::string type;
    std::string semantic_role;
    PresetInspection preset;
    std::string asset;
    std::string source;
    std::string text;
    FontInspection font;
    std::optional<float> font_size;
    std::string fill;
    std::string stroke;
    std::string background;
    LayoutInspection layout;
    MotionInspection motion;
    std::string blend_mode;
    std::optional<float> opacity;
    bool loop{false};
    std::optional<int> start_frame;
    std::optional<int> duration_frames;
};

struct ResolvedRenderPlanInspection {
    JobInspection job;
    CanvasInspection canvas;
    std::string output_path;
    std::string output_format;
    std::string video_codec;
    std::vector<ResolvedLayerInspection> layers;
};

/// Build the read-only inspection view from a prepared render plan context.
/// Pure derivation: never mutates the context and never touches the renderer
/// execution path.
[[nodiscard]] ResolvedRenderPlanInspection
build_render_plan_inspection(const PreparedRenderPlanContext& context);

} // namespace chronon3d::cli
