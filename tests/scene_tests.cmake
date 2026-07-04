# ── Scene Tests ──

# Text/Blend2D-dependent test sources
set(SCENE_TEXT_TESTS "")
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    list(APPEND SCENE_TEXT_TESTS
        scene/layout/test_layer_builder_animated.cpp
        layout/test_design_kit.cpp
        text/test_text_run_builder.cpp
    )
endif()

add_executable(chronon3d_scene_tests
    ${TEST_MAIN}
    scene/layout/test_scene_builder.cpp
    scene/shapes/test_path_utils.cpp
    scene/shapes/test_shape_model.cpp
    scene/layout/test_layer_hierarchy.cpp
    ${SCENE_TEXT_TESTS}
    scene/rendering/test_render_node_factory.cpp
    scene/rendering/test_depth_role.cpp
    scene/layout/test_layout_solver.cpp
    layout/test_layout_flow_grid.cpp
    presets/test_presets.cpp
    presets/test_layer_motion_presets.cpp
    render_graph/pipeline/test_graph_cache.cpp
    extension/test_graph_node_catalog.cpp
    scene/transform_hierarchy_tests.cpp
    scene/camera_rig_tests.cpp
    scene/camera_shot_validator_tests.cpp
    scene/camera_projection_tests.cpp
    scene/camera_framing_tests.cpp
    scene/camera_path_sampler_tests.cpp
    scene/camera/test_camera_registry.cpp
    scene/camera/test_camera_program.cpp
    scene/camera/test_camera_program_compiled.cpp   # CAM-01 / DOC 04: baseline compiled-path tests
    scene/camera/test_camera_descriptor_adapters.cpp
    scene/camera/test_composition_default_camera.cpp   # TICKET-034: CameraDescriptor as canonical default in composition settings
    scene/camera/test_camera_constraints_p5.cpp
    scene/camera/test_camera_session_checkpoint.cpp   # TICKET-031 — stateful constraint checkpoint + canonical pre-roll
    # TICKET-A3-DAMPED-HISTORY (Agent3 mission DoD gate (b)) — regression
    # lock that the presence of DampedFollowConstraint in
    # descriptor.constraints forces CameraProgram::evaluation_dependency()
    # to CameraEvaluationDependency::RequiresHistory, regardless of any
    # other indicator (source variant, modifier list, other constraint
    # types). Pairs with the force-override rule introduced in
    # src/scene/camera/camera_v1/camera_program_compiler.cpp on the same
    # commit.
    scene/camera/test_camera_program_damped_history_force.cpp
    scene/camera/test_camera_program_metadata_late_rebuild.cpp
    scene/camera/test_camera_framing_solver.cpp
    # TICKET-A3-LOOKAT-DIAGNOSTIC (Agent3 mission DoD gate (g)) — locks
    # the contract that LookAtLayer orientation emits a Warning diagnostic
    # via the canonical channel (result.diagnostics) with a stable
    # [MissingTransforms] prefix when CameraEvalContext::transforms is
    # null OR world_position lookup fails. Pairs with the diagnostic
    # emission introduced in src/scene/camera/camera_v1/camera_program.cpp
    # apply_orientation_spec_free() on the same commit.
    scene/camera/test_camera_lookat_layer_missing_transforms.cpp
    scene/camera/test_shot_timeline.cpp
    scene/camera/test_camera_trajectory.cpp
    # TICKET-A3-CTX-FRAMERATE (Agent3 mission DoD gate (e)) — locks the
    # public contract that camera_v1::*Context::at(Frame, FrameRate, ...)
    # factories propagate the caller-supplied FrameRate bit-exactly into
    # SampleTime arithmetic, with NO default-init fallback.  Pairs with
    # the previous kTimelineFps{30,1} constexpr-removal in
    # src/scene/camera/camera_v1/shot_timeline.cpp on the same commit.
    scene/camera/test_camera_context_framerate_propagation.cpp
    scene/camera/test_orient_along_path.cpp   # Agent1 DoD: 4-sub-test OrientAlongPath regression lock (Step 4)
    scene/camera/test_camera_stabilization.cpp
    scene/camera/test_camera_projection_contract.cpp
    scene/camera/golden_projection_test.cpp   # C7 — DOC 02 / TICKET-035 freeze: 6-mode golden projection
    scene/camera/test_camera_near_plane_clip.cpp
    scene/camera/camera_25d_tests.cpp
    scene/camera/test_camera_hierarchy.cpp
    scene/camera/test_camera_motion_blur.cpp
    scene/camera/test_temporal_samples_pr1.cpp   # PR1 unit: temporal_samples API contract
    scene/camera/test_motion_blur_torture_pr1.cpp   # PR1 torture: 5 mandatory end-to-end tests
    scene/camera/test_camera_motion_path.cpp
    scene/camera/test_catmull_rom_path.cpp
    scene/test_scene_validator.cpp
    scene/test_layer_order_contract.cpp
    scene/test_layer_kind.cpp
    scene/test_joints.cpp
    scene/test_motion_graph_prewarm.cpp
    render_graph/builder/test_graph_snapshot.cpp
)
target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_scene chronon3d_backend_software doctest::doctest)
# TICKET-006: SCENE_TEXT_TESTS (test_layer_builder_animated, layout_design_kit,
# test_text_run_builder) call into chronon3d_text_core symbols whose definitions
# live in chronon3d_backend_text. Without this guard the linker errors with:
#   'undefined symbol: chronon3d::shape_resolved_run(...)'
#   'undefined symbol: chronon3d::text_run_materialize(...)'
# Mirrors the same guard in tests/core_tests.cmake. Both targets legitimately
# need backend_text because CORE_BLEND2D_TESTS and SCENE_TEXT_TESTS exercise
# different but overlapping text paths.
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_backend_text)
endif()
target_include_directories(chronon3d_scene_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_scene_tests)
add_test(NAME chronon3d_scene_tests COMMAND chronon3d_scene_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── CAM-DOC 04 — 5 mandatory camera_v1 determinism test categories ──
# Compiled-execution / parity / golden struct hash / serial-vs-parallel /
# random-access determinism.  See tests/scene/camera/test_camera_compiled_evaluate.cpp.
add_executable(chronon3d_camera_compiled_evaluate_tests
    ${TEST_MAIN}
    scene/camera/test_camera_compiled_evaluate.cpp
)
target_link_libraries(chronon3d_camera_compiled_evaluate_tests PRIVATE
    chronon3d_sdk chronon3d_sdk_impl chronon3d_scene doctest::doctest)
target_include_directories(chronon3d_camera_compiled_evaluate_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_camera_compiled_evaluate_tests)
add_test(NAME chronon3d_camera_compiled_evaluate_tests
         COMMAND chronon3d_camera_compiled_evaluate_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# CAM-DOC 04 architecture boundary gate — shadows the bash script so
# `ctest -L camera_architecture_gate` runs the lint before any of the
# scene tests below.  Fails the gate on legacy AnimatedCamera2_5D /
# CameraRig / SceneBuilder animated_camera usages, tan(fov) outside
# camera_math/, or compile_camera() in hot paths.  See
# tools/check_camera_architecture.sh for the rule definitions.
add_test(NAME chronon3d_camera_architecture_gate
         COMMAND ${CMAKE_SOURCE_DIR}/tools/check_camera_architecture.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(chronon3d_camera_architecture_gate
    PROPERTIES LABELS "camera_architecture_gate;gate"
    TIMEOUT 30)

# ── P3-F — Composition camera unification test suite ──
# 7 mandatory end-to-end tests validating that Composition has NO mutable
# camera state and the V2 pipeline (CompositionDefinition →
# compile_composition → evaluate → EvaluatedCompositionFrame) is the
# canonical consume path.  See
# tests/scene/camera/test_composition_camera_unification.cpp for details.
add_executable(chronon3d_composition_camera_unification_tests
    ${TEST_MAIN}
    scene/camera/test_composition_camera_unification.cpp
)
target_link_libraries(chronon3d_composition_camera_unification_tests PRIVATE
    chronon3d_sdk chronon3d_sdk_impl chronon3d_scene doctest::doctest)
target_include_directories(chronon3d_composition_camera_unification_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_composition_camera_unification_tests)
add_test(NAME chronon3d_composition_camera_unification_tests
         COMMAND chronon3d_composition_camera_unification_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
