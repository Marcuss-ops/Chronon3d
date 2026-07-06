// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_builtin_presets.hpp
//
// FASE 6 Step 1 — 19 legacy camera presets extracted from
// camera_program_compiler.cpp.
//
// builtin_camera_presets() returns all legacy presets as CameraDescriptors,
// enabling the compiled evaluation path without CameraMotionRegistry.
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>

#include <span>

namespace chronon3d::camera_v1 {

/// All 19 legacy presets as CameraDescriptors with PoseTracksSource or
/// OrbitMotion sources, ready for compile_camera().
std::span<const NamedCameraPreset> builtin_camera_presets();

} // namespace chronon3d::camera_v1
