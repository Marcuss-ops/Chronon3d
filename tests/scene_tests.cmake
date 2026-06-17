# ── Scene Tests ──

add_executable(chronon3d_scene_tests
    ${TEST_MAIN}
    scene/layout/test_scene_builder.cpp
    scene/shapes/test_path_utils.cpp
    scene/shapes/test_shape_model.cpp
    scene/layout/test_layer_hierarchy.cpp
    scene/camera/test_camera_hierarchy.cpp
    scene/layout/test_layer_builder_animated.cpp
    scene/rendering/test_render_node_factory.cpp
    layout/test_design_kit.cpp
    scene/rendering/test_depth_role.cpp
    scene/layout/test_layout_solver.cpp
    presets/test_presets.cpp
    presets/test_layer_motion_presets.cpp
    render_graph/pipeline/test_graph_cache.cpp
    scene/transform_hierarchy_tests.cpp
    scene/camera_rig_tests.cpp
    scene/camera_shot_validator_tests.cpp
    scene/camera_projection_tests.cpp
    scene/camera_framing_tests.cpp
    scene/camera_path_sampler_tests.cpp
    scene/camera/test_camera_stabilization.cpp
    scene/camera/test_camera_trajectory.cpp
    scene/camera/test_camera_registry.cpp
    scene/camera/test_camera_program.cpp
    scene/camera/test_camera_constraints_p5.cpp
    scene/camera/test_camera_framing_solver.cpp
    scene/camera/test_shot_timeline.cpp
    scene/camera/test_camera_motion_blur.cpp
    scene/camera/test_camera_projection_contract.cpp
    scene/camera/test_camera_near_plane_clip.cpp
    scene/camera/test_camera_motion_path.cpp
    scene/camera/test_catmull_rom_path.cpp
    scene/test_scene_validator.cpp
    scene/test_layer_order_contract.cpp
    render_graph/builder/test_graph_snapshot.cpp
    specscene/test_specscene_compile_contract.cpp
)
target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_pipeline chronon3d_scene chronon3d_backend_software doctest::doctest)
target_include_directories(chronon3d_scene_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_scene_tests)
add_test(NAME chronon3d_scene_tests COMMAND chronon3d_scene_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
