#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/model/fill.hpp>
#include <chronon3d/scene/model/shape.hpp>
#include <chronon3d/scene/model/render_runtime.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>
#include <variant>

namespace chronon3d {

class FontEngine;  // forward declaration

// Soft drop shadow drawn behind the shape.
struct DropShadow {
    bool  enabled{false};
    Vec2  offset{0.0f, 8.0f};
    Color color{0.0f, 0.0f, 0.0f, 0.35f};
    f32   radius{12.0f};
};

// Soft glow emanating outward from the shape.
struct Glow {
    bool  enabled{false};
    f32   radius{15.0f};
    f32   intensity{0.8f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

// What kind of surface the node naturally produces.
// IntrinsicSize: content-driven layers such as text, shapes and images.
// ViewportSize: full-canvas layers such as grid backgrounds.
// EffectBounds: layers whose bounds are expanded by effects.
enum class SurfacePolicy {
    IntrinsicSize,
    EffectBounds,
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
    Glow glow;
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

    explicit RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

} // namespace chronon3d
