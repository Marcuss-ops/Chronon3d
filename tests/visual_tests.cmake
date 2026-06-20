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
