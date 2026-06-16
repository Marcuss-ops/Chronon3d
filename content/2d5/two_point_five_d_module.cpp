#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::two_point_five_d {

// ── Forward declarations (definitions in two_point_five_d_compositions.cpp) ──

Composition parallax_simple();
Composition depth_scene();
Composition card_flip();
Composition camera_orbit_target_lock_test();
Composition camera_dolly_perspective_scale_test();
Composition camera_parent_null_rig_test();
Composition camera_roll_pan_tilt_grid_test();
Composition camera_safe_framing_aspect_ratio_16_9();
Composition camera_safe_framing_aspect_ratio_1_1();
Composition camera_safe_framing_aspect_ratio_9_16();
Composition camera_safe_framing_aspect_ratio_4_5();
Composition camera_frustum_culling_precision_test();
Composition camera_kinematic_jerk_interpolation_test();
Composition camera_depth_sorting_stress_test();
Composition camera_subpixel_jitter_validation_test();
Composition camera_multi_target_bounding_box_fit_test();
Composition camera_depth_perspective_scale_diagnostic_test();
Composition camera_coordinate_contract_test();
Composition camera_binding_anchor_test();
Composition camera_front_baseline_test();
Composition camera_yaw_positive_test();
Composition camera_yaw_negative_test();
Composition camera_pitch_positive_test();
Composition camera_pitch_negative_test();
Composition camera_near_plane_crossing_test();
Composition camera_floor_grid_comparison_test();
Composition dof_showcase();

} // namespace chronon3d::content::two_point_five_d

namespace chronon3d {

class TwoPointFiveDModule : public ExtensionModule {
public:
    std::string_view id() const override { return "2d5"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::two_point_five_d;

        // Core 2.5D scenes
        registry.register_composition("ParallaxSimple", parallax_simple);
        registry.register_composition("DepthScene", depth_scene);
        registry.register_composition("CardFlip", card_flip);
        registry.register_composition("DofShowcase", dof_showcase);

        // Camera tests
        registry.register_composition("CameraOrbitTargetLockTest", camera_orbit_target_lock_test);
        registry.register_composition("CameraDollyPerspectiveScaleTest", camera_dolly_perspective_scale_test);
        registry.register_composition("CameraParentNullRigTest", camera_parent_null_rig_test);
        registry.register_composition("CameraRollPanTiltGridTest", camera_roll_pan_tilt_grid_test);
        registry.register_composition("CameraSafeFramingAspectRatioTest_16_9", camera_safe_framing_aspect_ratio_16_9);
        registry.register_composition("CameraSafeFramingAspectRatioTest_1_1", camera_safe_framing_aspect_ratio_1_1);
        registry.register_composition("CameraSafeFramingAspectRatioTest_9_16", camera_safe_framing_aspect_ratio_9_16);
        registry.register_composition("CameraSafeFramingAspectRatioTest_4_5", camera_safe_framing_aspect_ratio_4_5);
        registry.register_composition("CameraFrustumCullingPrecisionTest", camera_frustum_culling_precision_test);
        registry.register_composition("CameraKinematicJerkAndInterpolationTest", camera_kinematic_jerk_interpolation_test);
        registry.register_composition("CameraDepthSortingStressTest", camera_depth_sorting_stress_test);
        registry.register_composition("CameraSubpixelJitterValidationTest", camera_subpixel_jitter_validation_test);
        registry.register_composition("CameraMultiTargetBoundingBoxFitTest", camera_multi_target_bounding_box_fit_test);
        registry.register_composition("CameraDepthPerspectiveScaleDiagnosticTest", camera_depth_perspective_scale_diagnostic_test);

        // Diagnostic coordinate tests
        registry.register_composition("CameraCoordinateContractTest", camera_coordinate_contract_test);
        registry.register_composition("CameraBindingAnchorTest", camera_binding_anchor_test);

        // Yaw baseline tests
        registry.register_composition("CameraFrontBaselineTest", camera_front_baseline_test);
        registry.register_composition("CameraYawPositiveTest", camera_yaw_positive_test);
        registry.register_composition("CameraYawNegativeTest", camera_yaw_negative_test);

        // Pitch tests
        registry.register_composition("CameraPitchPositiveTest", camera_pitch_positive_test);
        registry.register_composition("CameraPitchNegativeTest", camera_pitch_negative_test);

        // Near-plane crossing test
        registry.register_composition("CameraNearPlaneCrossingTest", camera_near_plane_crossing_test);

        // Floor grid comparison test (Y=+250 vs Y=-250)
        registry.register_composition("CameraFloorGridComparisonTest", camera_floor_grid_comparison_test);
    }
};

std::unique_ptr<ExtensionModule> create_two_point_five_d_module() {
    return std::make_unique<TwoPointFiveDModule>();
}

} // namespace chronon3d
