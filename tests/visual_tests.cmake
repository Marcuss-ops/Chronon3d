# ── Visual Test Support Library ──
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

# Test the support library itself.
add_executable(chronon3d_visual_test_support_tests
    ${TEST_MAIN}
    visual/support/test_visual_support.cpp
)

target_link_libraries(chronon3d_visual_test_support_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_backend_image
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_visual_test_support_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(chronon3d_visual_test_support_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)

set_target_properties(chronon3d_visual_test_support_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_visual_test_support_tests)
add_test(NAME chronon3d_visual_test_support_tests
    COMMAND chronon3d_visual_test_support_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ── Camera Visual Regression Tests ──
# Golden image tests for camera behavior: center, parallax, orbit, near-plane, z-sort.

add_executable(chronon3d_camera_visual_tests
    ${TEST_MAIN}
    visual/camera/camera_visual_compare.cpp
    visual/camera/camera_visual_scenes.cpp
    visual/camera/camera_visual_tests.cpp
)

target_link_libraries(chronon3d_camera_visual_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_camera_visual_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_camera_visual_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_camera_visual_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_camera_visual_tests)
add_test(NAME chronon3d_camera_visual_tests COMMAND chronon3d_camera_visual_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Cinematic Motion Visual Regression Tests ──
# Diagnostic visual tests for SampleTime sub-frame, CubicBezier3D handles,
# arc-length spacing, temporal/spatial curve separation, cache parity.

add_executable(chronon3d_cinematic_motion_visual_tests
    ${TEST_MAIN}
    visual/cinematic_motion/cinematic_motion_scenes.cpp
    visual/cinematic_motion/cinematic_motion_scenes_bezier.cpp
    visual/cinematic_motion/cinematic_motion_scenes_quat.cpp
    visual/cinematic_motion/cinematic_motion_compare.cpp
    visual/cinematic_motion/cinematic_motion_tests.cpp
)

target_link_libraries(chronon3d_cinematic_motion_visual_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_cinematic_motion_visual_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_cinematic_motion_visual_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_cinematic_motion_visual_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_cinematic_motion_visual_tests)
add_test(NAME chronon3d_cinematic_motion_visual_tests COMMAND chronon3d_cinematic_motion_visual_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Render-Graph Node Golden Tests ──
# PR2 — 6 golden PNG snapshots for the 4 node categories
# (ShadowNode ×2, PerPixelDofNode, MaskNode ×2, GlowPipeline).

add_executable(chronon3d_render_graph_node_visual_tests
    ${TEST_MAIN}
    visual/render_graph/node_goldens.cpp
)

target_link_libraries(chronon3d_render_graph_node_visual_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_render_graph_node_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(chronon3d_render_graph_node_visual_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)

set_target_properties(chronon3d_render_graph_node_visual_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_render_graph_node_visual_tests)
add_test(NAME chronon3d_render_graph_node_visual_tests
    COMMAND chronon3d_render_graph_node_visual_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ── PR3 End-to-End Composition Visual Regression Tests ──
# 4 end-to-end compositions (text+gradient+mask, camera+depth+DoF,
# motion blur+transparencies, video+images+RTL text), each rendered as
# a none-vs-effect-on golden pair (8 PNGs total) under tests/golden/pr3/.

add_executable(chronon3d_pr3_composition_visual_tests
    ${TEST_MAIN}
    visual/PR3/pr3_compositions.cpp
)

target_link_libraries(chronon3d_pr3_composition_visual_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_pr3_composition_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(chronon3d_pr3_composition_visual_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)

set_target_properties(chronon3d_pr3_composition_visual_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_pr3_composition_visual_tests)
add_test(NAME chronon3d_pr3_composition_visual_tests
    COMMAND chronon3d_pr3_composition_visual_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ── AE Parity Camera Visual Regression Tests ──
# 10 AE parity scenes for visual comparison Chronon3D ↔ After Effects.
# Golden PNGs in tests/golden/ae_parity/; diffs in tests/golden/ae_parity/diff/.
# Set CHRONON3D_UPDATE_GOLDENS=1 to create / update golden PNGs.

add_executable(chronon3d_ae_parity_tests
    ${TEST_MAIN}
    visual/ae_parity/ae_parity_tests.cpp
    visual/ae_parity/ae_parity_scenes.cpp
)
# ae_parity_scenes.cpp was previously linked via chronon3d_content OBJECT
# library; that link broke post content-dedup.  Compiling it directly
# into the test binary restores the symbols without duplicating the TU
# across the CLI binary (which also compiles it).

target_link_libraries(chronon3d_ae_parity_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_ae_parity_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(chronon3d_ae_parity_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)

set_target_properties(chronon3d_ae_parity_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_ae_parity_tests)
add_test(NAME chronon3d_ae_parity_tests
    COMMAND chronon3d_ae_parity_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
