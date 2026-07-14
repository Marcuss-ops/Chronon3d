# ── Visual Test Support Library ──
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# `chronon3d_visual_test_support` is a STATIC library PUBLIC-linkined to
# chronon3d_backend_software + chronon3d_backend_image (both Blend2D-bearing).
# Self-guarding here keeps SDK-only builds free of the visual test
# dependency surface (no `chronon3d_visual_test_support` target requested).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()
# Shared golden-image and image-diff framework for all visual tests.

add_library(chronon3d_visual_test_support STATIC
    visual/support/image_diff.cpp
    visual/support/golden_test.cpp
)

target_link_libraries(chronon3d_visual_test_support
    PUBLIC
        chronon3d_backend_software
        chronon3d_backend_image
)

target_include_directories(chronon3d_visual_test_support
    PUBLIC ${CMAKE_SOURCE_DIR}
)

# ── Visual Test Support Tests ──
# Tests the support library itself (image_diff, golden_test).
chronon3d_add_test_suite(
    NAME chronon3d_visual_test_support_tests
    TIER INTEGRATION
    SOURCES visual/support/test_visual_support.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_backend_image
        chronon3d_scene
)
target_compile_definitions(chronon3d_visual_test_support_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── Camera Visual Regression Tests ──
# Golden image tests for camera behavior: center, parallax, orbit, near-plane, z-sort.
chronon3d_add_test_suite(
    NAME chronon3d_camera_visual_tests
    TIER INTEGRATION
    SOURCES
        visual/camera/camera_visual_compare.cpp
        visual/camera/camera_visual_scenes.cpp
        visual/camera/camera_visual_tests.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
)
target_compile_definitions(chronon3d_camera_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── Cinematic Motion Visual Regression Tests ──
# Diagnostic visual tests for SampleTime sub-frame, CubicBezier3D handles,
# arc-length spacing, temporal/spatial curve separation, cache parity.
chronon3d_add_test_suite(
    NAME chronon3d_cinematic_motion_visual_tests
    TIER INTEGRATION
    SOURCES
        visual/cinematic_motion/cinematic_motion_scenes.cpp
        visual/cinematic_motion/cinematic_motion_scenes_bezier.cpp
        visual/cinematic_motion/cinematic_motion_scenes_quat.cpp
        visual/cinematic_motion/cinematic_motion_compare.cpp
        visual/cinematic_motion/cinematic_motion_tests.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
)
target_compile_definitions(chronon3d_cinematic_motion_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── Render-Graph Node Golden Tests ──
# PR2 — 6 golden PNG snapshots for the 4 node categories
# (ShadowNode ×2, PerPixelDofNode, MaskNode ×2, GlowPipeline).
chronon3d_add_test_suite(
    NAME chronon3d_render_graph_node_visual_tests
    TIER INTEGRATION
    SOURCES visual/render_graph/node_goldens.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
)
target_compile_definitions(chronon3d_render_graph_node_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── PR3 End-to-End Composition Visual Regression Tests ──
# 4 end-to-end compositions (text+gradient+mask, camera+depth+DoF,
# motion blur+transparencies, video+images+RTL text), each rendered as
# a none-vs-effect-on golden pair (8 PNGs total) under tests/golden/pr3/.
chronon3d_add_test_suite(
    NAME chronon3d_pr3_composition_visual_tests
    TIER INTEGRATION
    SOURCES visual/PR3/pr3_compositions.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
)
target_compile_definitions(chronon3d_pr3_composition_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── AE Parity Camera Visual Regression Tests ──
# 10 AE parity scenes for visual comparison Chronon3D ↔ After Effects.
# Golden PNGs in tests/golden/ae_parity/; diffs in tests/golden/ae_parity/diff/.
# Set CHRONON3D_UPDATE_GOLDENS=1 to create / update golden PNGs.
#
# ae_parity_scenes.cpp was previously linked via chronon3d_content OBJECT
# library; that link broke post content-dedup.  Compiling it directly
# into the test binary restores the symbols without duplicating the TU
# across the CLI binary (which also compiles it).
chronon3d_add_test_suite(
    NAME chronon3d_ae_parity_tests
    TIER INTEGRATION
    SOURCES
            visual/ae_parity/ae_parity_tests.cpp
        ${CMAKE_SOURCE_DIR}/content/ae_parity/ae_cam_scenes.cpp
        # TICKET-CHRONON-GLOW-FINAL — Phase 3 SCALA regression:
        # 6 TEST_CASEs (3 frames × 2 ARs) asserting alpha-bbox centroid
        # stays within 2 px of canvas center despite per-frame scale
        # breath (0.96/1.05/0.98). Locks the CanvasCenter-based centroid
        # contract from drift caused by non-identity layer transforms.
        visual/ae_parity/ae_glow_position_drift.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
)
target_compile_definitions(chronon3d_ae_parity_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# TICKET-CHRONON-GLOW-FINAL — Phase 3 SCALA ctest alias.
# 6 TEST_CASEs: 16:9 + 9:16 × 3 snapshot frames (f00/f15/f30) verifying
# alpha-bbox centroid stability under scale 0.96/1.05/0.98.
add_test(
    NAME Ae08GlowPositionDrift
    COMMAND chronon3d_ae_parity_tests --test-case="FASE-3 SCALA: ae_08 *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ── Gate 1 — Timeline Visual Golden Tests ──
# 4 tests proving sequence boundaries, local frame mapping,
# animation-from-local-frame, and nested sequence mapping
# work correctly at the render level.
chronon3d_add_test_suite(
    NAME chronon3d_timeline_visual_tests
    TIER INTEGRATION
    SOURCES visual/timeline/test_timeline_golden.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
    LABELS gate
)
target_compile_definitions(chronon3d_timeline_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ── Gate 2 — Media Time Visual Tests ──
# 3 tests proving trim_before (source_start), playback_rate (speed),
# and freeze_frame work correctly at the map_video_frame level.
chronon3d_add_test_suite(
    NAME chronon3d_media_time_tests
    TIER INTEGRATION
    SOURCES visual/timeline/test_media_time_golden.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_scene
    LABELS gate
)
# Gate 2-4 don't link chronon3d_visual_test_support so they don't
# inherit ${CMAKE_SOURCE_DIR} transitively.  Add it explicitly for
# parity with the raw target include dirs.
target_include_directories(chronon3d_media_time_tests PRIVATE ${CMAKE_SOURCE_DIR})

# ── Gate 3 — Asset Readiness Tests ──
# 4 tests proving missing font/image/video cause hard preflight failure,
# and FrameOnly vs FullComposition scoping works correctly.
chronon3d_add_test_suite(
    NAME chronon3d_asset_readiness_tests
    TIER INTEGRATION
    SOURCES visual/timeline/test_asset_readiness.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_scene
        nlohmann_json::nlohmann_json
    LABELS gate
)
target_include_directories(chronon3d_asset_readiness_tests PRIVATE ${CMAKE_SOURCE_DIR})

# ── Gate 4 — Debug Timeline Overlay Tests ──
# 4 tests proving TimelineDebugInfo model captures active sequences,
# local frames, and assets_used with valid JSON serialization.
chronon3d_add_test_suite(
    NAME chronon3d_debug_overlay_tests
    TIER INTEGRATION
    SOURCES visual/timeline/test_debug_overlay.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_scene
        nlohmann_json::nlohmann_json
    LABELS gate
)
target_include_directories(chronon3d_debug_overlay_tests PRIVATE ${CMAKE_SOURCE_DIR})
