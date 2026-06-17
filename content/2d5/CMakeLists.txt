add_library(chronon3d_content_2d5 STATIC
    two_point_five_d_module.cpp
    compositions/two_point_five_d_compositions.cpp
    compositions/camera_calibration_scene.cpp
    compositions/camera_advanced_tests_scene.cpp
    compositions/camera_advanced_tests_rig.cpp
    compositions/camera_advanced_tests_diag.cpp
    compositions/camera_test_orchestrator.cpp
    compositions/camera_pitch_tests.cpp
    compositions/camera_near_plane_test.cpp
    compositions/camera_floor_grid_test.cpp
)
set_target_properties(chronon3d_content_2d5 PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_2d5 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_2d5
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_2d5 PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_2d5 ALIAS chronon3d_content_2d5)
