#pragma once

#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/scene/scene.hpp>

namespace chronon3d {

// LayoutSolver applies LayoutRules to a Scene before rendering.
// It mutates Layer::transform.position in-place.
//
// Pipeline:
//   SceneBuilder::build() -> Scene
//   LayoutSolver::solve(scene, width, height)
//   -> SoftwareRenderer::render_scene(scene, ...)
class LayoutSolver {
public:
    void solve(Scene& scene, i32 canvas_width, i32 canvas_height) const;
};

// Compute the 2D position that places `anchor` of a layer at the given canvas point.
Vec2 anchor_position(Anchor anchor, i32 canvas_width, i32 canvas_height, f32 margin);

} // namespace chronon3d
