#pragma once

#include <chronon3d/math/camera_2_5d_projection.hpp>

// Test-only convenience. Production call sites must declare the actual
// surface size explicitly at the eight-argument projection boundary.
inline chronon3d::ProjectedLayer2_5D project_test_layer_2_5d(
    const chronon3d::Transform& layer,
    const chronon3d::Camera2_5D& camera,
    chronon3d::f32 viewport_width,
    chronon3d::f32 viewport_height) {
    return chronon3d::project_layer_2_5d(
        layer, layer.to_mat4(), camera, viewport_width, viewport_height,
        false, {viewport_width, viewport_height},
        chronon3d::BackfaceMode::DoubleSided);
}
