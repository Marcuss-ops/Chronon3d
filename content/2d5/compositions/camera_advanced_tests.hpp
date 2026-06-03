#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::two_point_five_d {

Composition camera_frustum_culling_precision_test();
Composition camera_kinematic_jerk_interpolation_test();
Composition camera_depth_sorting_stress_test();
Composition camera_subpixel_jitter_validation_test();
Composition camera_multi_target_bounding_box_fit_test();

Composition camera_orbit_target_lock_test();
Composition camera_dolly_perspective_scale_test();
Composition camera_parent_null_rig_test();
Composition camera_roll_pan_tilt_grid_test();
Composition camera_safe_framing_aspect_ratio_16_9();
Composition camera_safe_framing_aspect_ratio_1_1();
Composition camera_safe_framing_aspect_ratio_9_16();
Composition camera_safe_framing_aspect_ratio_4_5();

Composition perspective_depth_showcase();

} // namespace chronon3d::content::two_point_five_d
