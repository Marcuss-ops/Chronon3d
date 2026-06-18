// Content registration — single direct registration of all built-in compositions.
// Product compositions register unconditionally.
// Diagnostic compositions (tests, calibrations, A/B) only register when
// CHRONON3D_BUILD_DIAGNOSTICS is defined.
#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

// ── Minimalist ────────────────────────────────────────────────────────────────
namespace chronon3d::content::minimalist {
Composition minimalist_text_fade_up();
Composition minimalist_text_tracking_reveal();
Composition minimalist_text_clip_reveal();
Composition minimalist_text_fade_down();
Composition minimalist_text_soft_scale();
Composition minimalist_text_blur_focus();
Composition minimalist_text_slide_left();
Composition minimalist_text_slide_right();
Composition minimalist_text_scale_pop();
Composition minimalist_text_float_in();
Composition minimalist_text_letter_rise();
Composition minimalist_text_drift_in();
Composition minimalist_text_tilt_in();
Composition minimalist_text_mask_reveal();
Composition minimalist_text_snap_pop();
Composition minimalist_text_fade_away();
Composition minimalist_text_scale_out();
Composition minimalist_text_slide_up_out();
Composition minimalist_text_blur_away();
Composition minimalist_text_tilt_out();
Composition minimalist_image_fade_in();
Composition minimalist_image_focus_in();
Composition minimalist_image_scale_drop();
Composition minimalist_image_fade_shift_vertical();
Composition minimalist_image_center_split();
Composition minimalist_image_reveal_from_bottom();
Composition minimalist_image_framing_bracket();
Composition minimalist_image_tracking_breathing();
Composition minimalist_image_elegant_exit();
Composition minimalist_image_elastic_slide();
}

// ── SpecialNames ──────────────────────────────────────────────────────────────
namespace chronon3d::content::special_names {
Composition special_name_fade_up();
Composition special_name_slide_left();
Composition special_name_slide_right();
Composition special_name_scale_in();
Composition special_name_stamp();
Composition special_name_blur_in();
Composition special_name_typewriter();
Composition special_name_split();
}

// ── ImportantWords ────────────────────────────────────────────────────────────
namespace chronon3d::content::important_words {
Composition important_word_director_light();
Composition important_word_actor_warm();
Composition important_word_writer_cool();
Composition important_word_trio();
}

// ── Shapes (proofs → diagnostics) ─────────────────────────────────────────────
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::shapes {
Composition shape_proofs();
Composition shape_motion_proofs();
}
#endif

// ── Images (product + diagnostics) ────────────────────────────────────────────
namespace chronon3d::content::images {
Composition img_shake_zoom();
Composition img_reference_shake_reveal();
Composition img_corner_smoothing();
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::images {
Composition img_gradient();
Composition img_checker();
Composition img_grid_test();
Composition img_test_pattern();
Composition image_proofs();
}
#endif

// ── Anims (product + diagnostic comparison) ───────────────────────────────────
namespace chronon3d::content::anims {
Composition anim_fade_in_text();
Composition anim_slide_text();
Composition anim_scale_text();
Composition anim_typewriter();
Composition anim_slide_up();
Composition anim_scale_pop();
Composition anim_blur_focus();
Composition anim_slide_left();
Composition anim_bounce_drop();
Composition anim_typewriter_simple();
Composition anim_typewriter_cursor();
Composition anim_typewriter_slide();
Composition anim_typewriter_glow();
Composition anim_typewriter_stagger();
Composition catmull_rom_showcase();
Composition dolly_zoom_showcase();
Composition tilt_sweep_title();
Composition tilt_sweep_title_v2();
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::anims {
Composition camera_spline_comparison();
}
#endif

// ── Effects (Glow + Thumbnails = product; A/B tests + Ref 2.5D = diagnostics) ─
namespace chronon3d::content::effects {
Composition glow_02_orb_galaxy();
Composition glow_basic_word();
Composition premium_thumbnail_buttery_smooth();
Composition premium_thumbnail_saas_blue();
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::effects {
Composition glow_sharpness_test();
Composition glow_paragraph_test();
Composition glow_radius_compare_test();
Composition glow_typewriter_reveal_test();
Composition glow_shadow_balance_test();
}
// ref_2_5d functions declared in effects/ref_2_5d/reference_2_5d_suite.hpp
#include "effects/ref_2_5d/reference_2_5d_suite.hpp"
#endif

// ── Grid ──────────────────────────────────────────────────────────────────────
namespace chronon3d::content::grid {
Composition grid_color_showcase();
}

// ── 2.5D (product + camera diagnostics) ───────────────────────────────────────
namespace chronon3d::content::two_point_five_d {
Composition parallax_simple();
Composition depth_scene();
Composition card_flip();
Composition dof_showcase();
}
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
// Camera test functions declared in 2d5/compositions/camera_advanced_tests.hpp
#include "2d5/compositions/camera_advanced_tests.hpp"
#endif

namespace chronon3d {

void register_content_modules() {
    auto& reg = ExtensionRegistry::instance();

    // ── Minimalist: text + image animations ─────────────────────────────────
    using namespace content::minimalist;
    reg.register_composition("MinimalistTextFadeUp",              minimalist_text_fade_up);
    reg.register_composition("MinimalistTextTrackingReveal",     minimalist_text_tracking_reveal);
    reg.register_composition("MinimalistTextClipReveal",         minimalist_text_clip_reveal);
    reg.register_composition("MinimalistTextFadeDown",           minimalist_text_fade_down);
    reg.register_composition("MinimalistTextSoftScale",          minimalist_text_soft_scale);
    reg.register_composition("MinimalistTextBlurFocus",          minimalist_text_blur_focus);
    reg.register_composition("MinimalistTextSlideLeft",          minimalist_text_slide_left);
    reg.register_composition("MinimalistTextSlideRight",         minimalist_text_slide_right);
    reg.register_composition("MinimalistTextScalePop",           minimalist_text_scale_pop);
    reg.register_composition("MinimalistTextFloatIn",            minimalist_text_float_in);
    reg.register_composition("MinimalistTextLetterRise",         minimalist_text_letter_rise);
    reg.register_composition("MinimalistTextDriftIn",            minimalist_text_drift_in);
    reg.register_composition("MinimalistTextTiltIn",             minimalist_text_tilt_in);
    reg.register_composition("MinimalistTextMaskReveal",         minimalist_text_mask_reveal);
    reg.register_composition("MinimalistTextSnapPop",            minimalist_text_snap_pop);
    reg.register_composition("MinimalistTextFadeAway",           minimalist_text_fade_away);
    reg.register_composition("MinimalistTextScaleOut",           minimalist_text_scale_out);
    reg.register_composition("MinimalistTextSlideUpOut",         minimalist_text_slide_up_out);
    reg.register_composition("MinimalistTextBlurAway",           minimalist_text_blur_away);
    reg.register_composition("MinimalistTextTiltOut",            minimalist_text_tilt_out);
    reg.register_composition("MinimalistImageFadeIn",            minimalist_image_fade_in);
    reg.register_composition("MinimalistImageFocusIn",           minimalist_image_focus_in);
    reg.register_composition("MinimalistImageScaleDrop",         minimalist_image_scale_drop);
    reg.register_composition("MinimalistImageFadeShiftVertical", minimalist_image_fade_shift_vertical);
    reg.register_composition("MinimalistImageCenterSplit",       minimalist_image_center_split);
    reg.register_composition("MinimalistImageRevealFromBottom",  minimalist_image_reveal_from_bottom);
    reg.register_composition("MinimalistImageFramingBracket",    minimalist_image_framing_bracket);
    reg.register_composition("MinimalistImageTrackingBreathing", minimalist_image_tracking_breathing);
    reg.register_composition("MinimalistImageElegantExit",       minimalist_image_elegant_exit);
    reg.register_composition("MinimalistImageElasticSlide",      minimalist_image_elastic_slide);

    // ── SpecialNames ─────────────────────────────────────────────────────
    {
        using namespace content::special_names;
        reg.register_composition("SpecialNameFadeUp",    special_name_fade_up);
        reg.register_composition("SpecialNameSlideLeft", special_name_slide_left);
        reg.register_composition("SpecialNameSlideRight",special_name_slide_right);
        reg.register_composition("SpecialNameScaleIn",   special_name_scale_in);
        reg.register_composition("SpecialNameStamp",     special_name_stamp);
        reg.register_composition("SpecialNameBlurIn",    special_name_blur_in);
        reg.register_composition("SpecialNameTypewriter", special_name_typewriter);
        reg.register_composition("SpecialNameSplit",     special_name_split);
    }

    // ── ImportantWords ───────────────────────────────────────────────────
    {
        using namespace content::important_words;
        reg.register_composition("ImportantWordDirectorLight", important_word_director_light);
        reg.register_composition("ImportantWordActorWarm",     important_word_actor_warm);
        reg.register_composition("ImportantWordWriterCool",    important_word_writer_cool);
        reg.register_composition("ImportantWordTrio",           important_word_trio);
    }

    // ── Images (product) ─────────────────────────────────────────────────
    {
        using namespace content::images;
        reg.register_composition("ImgShakeZoom",            img_shake_zoom);
        reg.register_composition("ImgReferenceShakeReveal", img_reference_shake_reveal);
        reg.register_composition("ImgCornerSmoothing",      img_corner_smoothing);
    }

    // ── Anims (product) ──────────────────────────────────────────────────
    {
        using namespace content::anims;
        reg.register_composition("AnimFadeInText",          anim_fade_in_text);
        reg.register_composition("AnimSlideText",           anim_slide_text);
        reg.register_composition("AnimScaleText",           anim_scale_text);
        reg.register_composition("AnimTypewriter",          anim_typewriter);
        reg.register_composition("AnimSlideUp",             anim_slide_up);
        reg.register_composition("AnimScalePop",            anim_scale_pop);
        reg.register_composition("AnimBlurFocus",           anim_blur_focus);
        reg.register_composition("AnimSlideLeft",           anim_slide_left);
        reg.register_composition("AnimBounceDrop",          anim_bounce_drop);
        reg.register_composition("AnimTypewriterSimple",    anim_typewriter_simple);
        reg.register_composition("AnimTypewriterCursor",    anim_typewriter_cursor);
        reg.register_composition("AnimTypewriterSlide",     anim_typewriter_slide);
        reg.register_composition("AnimTypewriterGlow",      anim_typewriter_glow);
        reg.register_composition("AnimTypewriterStagger",   anim_typewriter_stagger);
        reg.register_composition("CatmullRomShowcase",      catmull_rom_showcase);
        reg.register_composition("DollyZoomShowcase",       dolly_zoom_showcase);
        reg.register_composition("TiltSweepTitle",          tilt_sweep_title);
        reg.register_composition("TiltSweepTitleV2",        tilt_sweep_title_v2);
    }

    // ── Effects (product) ────────────────────────────────────────────────
    {
        using namespace content::effects;
        reg.register_composition("GlowOrbGalaxy",              glow_02_orb_galaxy);
        reg.register_composition("GlowBasicWord",              glow_basic_word);
        reg.register_composition("PremiumThumbnailButterySmooth", premium_thumbnail_buttery_smooth);
        reg.register_composition("PremiumThumbnailSaaSBlue",   premium_thumbnail_saas_blue);
    }

    // ── Grid ─────────────────────────────────────────────────────────────
    {
        using namespace content::grid;
        reg.register_composition("GridColorShowcase", grid_color_showcase);
    }

    // ── 2.5D (product) ───────────────────────────────────────────────────
    {
        using namespace content::two_point_five_d;
        reg.register_composition("ParallaxSimple",  parallax_simple);
        reg.register_composition("DepthScene",      depth_scene);
        reg.register_composition("CardFlip",        card_flip);
        reg.register_composition("DofShowcase",     dof_showcase);
    }

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    // ── Shapes (diagnostic proofs) ───────────────────────────────────────
    {
        using namespace content::shapes;
        reg.register_composition("ShapeProofs",       shape_proofs);
        reg.register_composition("ShapeMotionProofs", shape_motion_proofs);
    }

    // ── Images (diagnostic) ──────────────────────────────────────────────
    {
        using namespace content::images;
        reg.register_composition("ImgGradient",    img_gradient);
        reg.register_composition("ImgChecker",     img_checker);
        reg.register_composition("ImgGridTest",    img_grid_test);
        reg.register_composition("ImgTestPattern", img_test_pattern);
        reg.register_composition("ImageProofs",    image_proofs);
    }

    // ── Anims (diagnostic) ───────────────────────────────────────────────
    {
        using namespace content::anims;
        reg.register_composition("CameraSplineComparison", camera_spline_comparison);
    }

    // ── Effects (diagnostic: A/B tests + 2.5D Reference Suite) ───────────
    {
        using namespace content::effects;
        reg.register_composition("GlowSharpnessTest",          glow_sharpness_test);
        reg.register_composition("GlowParagraphTest",          glow_paragraph_test);
        reg.register_composition("GlowRadiusCompareTest",      glow_radius_compare_test);
        reg.register_composition("GlowTypewriterRevealTest",   glow_typewriter_reveal_test);
        reg.register_composition("GlowShadowBalanceTest",      glow_shadow_balance_test);

        // 2.5D Reference Suite (declared in content::effects via ref_2_5d header)
        reg.register_composition("FloatingCardsTest",          floating_cards_test);
        reg.register_composition("OrbitCameraTest",            orbit_camera_test);
        reg.register_composition("DepthFogTest",               depth_fog_test);
        reg.register_composition("ZStackParallaxTest",         z_stack_parallax_test);
        reg.register_composition("ShadowGlowConsistencyTest",  shadow_glow_consistency_test);
        reg.register_composition("ExtremePerspectiveTest",     extreme_perspective_test);
        reg.register_composition("YRotationTextTest",          y_rotation_text_test);
    }

    // ── 2.5D (diagnostic: camera tests) ──────────────────────────────────
    {
        using namespace content::two_point_five_d;
        reg.register_composition("CameraOrbitTargetLockTest",                camera_orbit_target_lock_test);
        reg.register_composition("CameraDollyPerspectiveScaleTest",          camera_dolly_perspective_scale_test);
        reg.register_composition("CameraParentNullRigTest",                  camera_parent_null_rig_test);
        reg.register_composition("CameraRollPanTiltGridTest",                camera_roll_pan_tilt_grid_test);
        reg.register_composition("CameraSafeFramingAspectRatioTest_16_9",    camera_safe_framing_aspect_ratio_16_9);
        reg.register_composition("CameraSafeFramingAspectRatioTest_1_1",     camera_safe_framing_aspect_ratio_1_1);
        reg.register_composition("CameraSafeFramingAspectRatioTest_9_16",    camera_safe_framing_aspect_ratio_9_16);
        reg.register_composition("CameraSafeFramingAspectRatioTest_4_5",     camera_safe_framing_aspect_ratio_4_5);
        reg.register_composition("CameraFrustumCullingPrecisionTest",        camera_frustum_culling_precision_test);
        reg.register_composition("CameraKinematicJerkAndInterpolationTest",  camera_kinematic_jerk_interpolation_test);
        reg.register_composition("CameraDepthSortingStressTest",             camera_depth_sorting_stress_test);
        reg.register_composition("CameraSubpixelJitterValidationTest",       camera_subpixel_jitter_validation_test);
        reg.register_composition("CameraMultiTargetBoundingBoxFitTest",      camera_multi_target_bounding_box_fit_test);
        reg.register_composition("CameraDepthPerspectiveScaleDiagnosticTest",camera_depth_perspective_scale_diagnostic_test);
        reg.register_composition("CameraCoordinateContractTest",             camera_coordinate_contract_test);
        reg.register_composition("CameraBindingAnchorTest",                  camera_binding_anchor_test);
        reg.register_composition("CameraFrontBaselineTest",                  camera_front_baseline_test);
        reg.register_composition("CameraYawPositiveTest",                    camera_yaw_positive_test);
        reg.register_composition("CameraYawNegativeTest",                    camera_yaw_negative_test);
    }
#endif
}

} // namespace chronon3d
