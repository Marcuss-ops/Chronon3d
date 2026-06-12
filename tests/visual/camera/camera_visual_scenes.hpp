#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <string>

namespace chronon3d::test {

Composition make_center_target_composition();
Composition make_parallax_stack_composition();
Composition make_orbit_two_node_composition();
Composition make_near_plane_crossing_composition();
Composition make_z_sort_stack_composition();

} // namespace chronon3d::test
