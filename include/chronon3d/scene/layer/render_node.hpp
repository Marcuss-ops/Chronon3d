#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/render_runtime.hpp>
#include <chronon3d/scene/shape.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>

namespace chronon3d {

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

struct RenderNode {
    std::pmr::string name;
    Transform world_transform;
    Color color{1, 1, 1, 1};
    Shape shape;
    DropShadow shadow;
    Glow glow;
    std::shared_ptr<Mesh> mesh;
    FakeBox3DRenderState fake_box3d_runtime;
    GridPlaneRenderState grid_plane_runtime;
    FakeExtrudedTextRenderState fake_extruded_text_runtime;
    bool visible{true};

    explicit RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

} // namespace chronon3d
