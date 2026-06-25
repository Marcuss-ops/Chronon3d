#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <string>
#include <vector>

namespace chronon3d::content::two_point_five_d {

/// Adds a full camera calibration / diagnostic scene to the SceneBuilder.
///
/// Every camera test composition should call this once before configuring
/// the CameraShotProfile and calling camera_test_orchestrator().
///
/// The scene contains:
///   - Dark gray background (not pure black)
///   - Ground grid plane at Y=-250
///   - Colored RGB axes at origin (X=red, Y=green, Z=blue)
///   - World-space target cross at origin
///   - Main asymmetric calibration card at Z=0 (500×350) with:
///       • Colored corners (red TL, green TR, blue BR)
///       • Yellow center dot
///       • Thick cyan border
///       • Asymmetric text labels (TOP, LEFT, FRONT →, RIGHT, BOTTOM)
///   - Three pillars at different Z depths (fake-box3d) [if include_pillars=true]:
///       pillar_near  → Z=-400  (cyan, tall)
///       pillar_mid   → Z=0     (blue, medium)
///       pillar_far   → Z=+400  (dark muted, short)
///   - A null layer named "camera_target" at origin
///   - A null layer named "camera_parent" at origin (for parent tests)
void add_camera_calibration_scene(SceneBuilder& s, bool include_pillars = true);

/// Returns the list of landmark layer names for auto-framing.
/// These cover the main card + three pillars + axes area.
[[nodiscard]] std::vector<std::string> calibration_landmark_layers();

} // namespace chronon3d::content::two_point_five_d
