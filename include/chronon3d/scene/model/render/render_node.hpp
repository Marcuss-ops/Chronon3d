#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/render/render_runtime.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>
#include <variant>

namespace chronon3d {

class FontEngine;  // forward declaration

// Forward declaration only — full definition in <chronon3d/text/text_run.hpp>.
// This avoids a header cycle: shape.hpp → text_run.hpp →
// text_animator_property.hpp → glyph_selector.hpp → animated_value.hpp →
// fill_style.hpp (circular!).  `std::shared_ptr<TextRunShape>` works with
// only a forward declaration because the deleter is type-erased at the
// `make_shared<TextRunShape>(…)` construction site.
struct TextRunShape;

// Soft drop shadow drawn behind the shape.
struct DropShadow {
    bool  enabled{false};
    Vec2  offset{0.0f, 8.0f};
    Color color{0.0f, 0.0f, 0.0f, 0.35f};
    f32   radius{12.0f};
};

// Soft glow emanating outward from the shape.
// Unified with GlowParams from effect_params.hpp — the single source of truth.
using Glow = GlowParams;

// What kind of surface the node naturally produces.
// IntrinsicSize: content-driven layers such as text, shapes and images.
// ViewportSize: full-canvas layers such as grid backgrounds.
enum class SurfacePolicy {
    IntrinsicSize,
    ViewportSize,
};

// How placement should be applied downstream.
// MatrixOnly means transforms should not force a new raster surface size.
// RasterizeAfter is reserved for future nodes that need a post-transform bake.
enum class TransformPolicy {
    MatrixOnly,
    RasterizeAfter,
};

struct RenderNode {
    std::pmr::string name;
    Transform world_transform;
    SurfacePolicy surface_policy{SurfacePolicy::IntrinsicSize};
    TransformPolicy transform_policy{TransformPolicy::MatrixOnly};
    Color color{1, 1, 1, 1};
    Fill fill{true, FillType::Solid, {1, 1, 1, 1}, {}};
    Shape shape;
    DropShadow shadow;
    GlowParams glow;
    std::shared_ptr<Mesh> mesh;
    f32 corner_radius{0.0f};

    // Runtime/shape-specific parameters to avoid hardcoding in RenderNode
    // Type-safe variant: stores FakeBox3DRenderState or GridPlaneRenderState
    using RenderNodeParams = std::variant<std::monostate, FakeBox3DRenderState, GridPlaneRenderState>;
    RenderNodeParams params;

    bool visible{true};

    // FontEngine pointer for precise text shaping / glyph metrics.
    // Set by LayerBuilder when the layer has a configured FontEngine.
    FontEngine* font_engine{nullptr};

    // ── TextRunShape slot (PR 3 — TextAnimator V2 integration) ──────────────
    // When `is_text_run_shape` is true AND `text_run_shape` is non-null, the
    // graph-builder source-pass routes this RenderNode to a TextRunNode
    // instead of a SourceNode.  The `m_shape` is mutated per-frame (per-glyph
    // animated state) so it is held as a non-const shared_ptr.
    //
    // When `is_text_run_shape` is false, this RenderNode behaves exactly as
    // before (shape-driven, SourceNode-backed).  This dual-mode flag keeps
    // PR 3 backwards-compatible with all existing shapes without forcing a
    // new ShapeType or LayerKind discriminator.
    bool is_text_run_shape{false};
    std::shared_ptr<TextRunShape> text_run_shape;

    explicit RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

} // namespace chronon3d
