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

# ── Gate 1 — Timeline Visual Golden Tests ──
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
chronon3d_add_test_suite(
    NAME chronon3d_media_time_tests
    TIER INTEGRATION
    SOURCES visual/timeline/test_media_time_golden.cpp
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_scene
    LABELS gate
)
target_include_directories(chronon3d_media_time_tests PRIVATE ${CMAKE_SOURCE_DIR})

# ── Gate 3 — Asset Readiness Tests ──
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

# ── Graphics Tests ──
chronon3d_add_test_suite(
    NAME chronon3d_graphics_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_scene chronon3d_backend_software chronon3d
    SOURCES graphics/test_gradient_sampler.cpp
            graphics/test_fill_style.cpp
            graphics/test_fill_style_integration.cpp
)

# ── Gradient Visual Golden Tests ──
chronon3d_add_test_suite(
    NAME chronon3d_gradient_visual_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_visual_test_support chronon3d_backend_software chronon3d_backend_image chronon3d_scene
    SOURCES visual/gradient_visual_tests.cpp
)
target_compile_definitions(chronon3d_gradient_visual_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ── Rounded Rect Visual Golden Tests ──
chronon3d_add_test_suite(
    NAME chronon3d_rounded_rect_visual_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_visual_test_support chronon3d_backend_software chronon3d_backend_image chronon3d_scene
    SOURCES visual/rounded_rect_visual_tests.cpp
)
target_compile_definitions(chronon3d_rounded_rect_visual_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ── Diagnostic Overlay Golden Tests ──
# Reuse the shared golden/image-diff library instead of recompiling those
# translation units directly into this executable.
chronon3d_add_test_suite(
    NAME chronon3d_diagnostic_overlay_tests
    TIER INTEGRATION
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_software
        chronon3d_content
        chronon3d_runtime
        chronon3d_text_core
        chronon3d_visual_test_support
    SOURCES text_golden/diagnostic_overlay/test_diagnostic_overlay.cpp
)
target_compile_definitions(chronon3d_diagnostic_overlay_tests PRIVATE
    CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
