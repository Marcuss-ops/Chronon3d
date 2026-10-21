# ── Scene Tests ──
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_particle_sampling_tests
    TIER UNIT
    SOURCES scene/shapes/test_particles_sampling.cpp
)

# ── TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0g+ ──
# Helper-specific UNIT-tier test coverage for
# `chronon3d::detail::image_params_resolve_path` (5 TEST_CASEs
# locking the canonical forwarding contract introduced by forward-
# point 0e + consolidated by forward-point 0f+).  UNCONDITIONAL
# (no Blend2D, no rendering backend, no SDK install surface) — must
# run on every CI invocation per cat-1 single-purpose atomic commit.
chronon3d_add_test_suite(
    NAME   chronon3d_image_params_resolve_path_tests
    TIER   UNIT
    SOURCES scene/builders/test_image_params_resolve_path.cpp
)

# Text/Blend2D-dependent test sources
set(SCENE_TEXT_TESTS "")
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    list(APPEND SCENE_TEXT_TESTS
        scene/layout/test_layer_builder_animated.cpp
        layout/test_design_kit.cpp
        text/test_text_run_builder.cpp
    )
endif()

# ── Shared link contract (§11.1 INTEGRATION tier) ────────────────────
set(_SCENE_LINK_TARGETS
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_scene
    chronon3d_backend_software
)

set(SCENE_TEST_SOURCES
                scene/layout/test_scene_builder.cpp
                scene/test_idle_motion_presets.cpp
                scene/test_text_motion_presets.cpp
                scene/test_fullscreen_rect_modular_bbox.cpp
                scene/shapes/test_path_utils.cpp
                scene/shapes/test_shape_model.cpp
                scene/layout/test_layer_hierarchy.cpp
                ${SCENE_TEXT_TESTS}
                scene/rendering/test_render_node_factory.cpp
                scene/rendering/test_depth_role.cpp
                scene/layout/test_layout_solver.cpp
                scene/layout/test_overlay_layout_resolver.cpp
                layout/test_layout_flow_grid.cpp
                render_graph/pipeline/test_graph_cache.cpp
                extension/test_graph_node_catalog.cpp
                scene/transform_hierarchy_tests.cpp
                scene/camera_projection_tests.cpp
                scene/camera/test_camera_registry.cpp
                scene/camera/test_camera_program.cpp
                scene/camera/test_camera_program_compiled.cpp
                scene/camera/test_camera_program_compiled_metadata.cpp
                scene/camera/test_camera_program_compiled_trajectory.cpp
                scene/camera/test_camera_program_orient_along_path.cpp
                scene/camera/test_camera_program_projection_orientation.cpp
                scene/camera/test_camera_program_orientation_ticket022.cpp
                scene/camera/test_camera_program_orbit_ticket024.cpp
                scene/camera/test_camera_program_orbit_coherence_roll_parent.cpp
                scene/camera/test_camera_program_constraints_policy.cpp
                scene/camera/test_camera_session_checkpoint.cpp
                scene/camera/test_camera_program_damped_history_force.cpp
                scene/camera/test_camera_lookat_layer_missing_transforms.cpp
                scene/camera/test_shot_timeline.cpp
                scene/camera/test_shot_timeline_random_access.cpp
                scene/camera/test_camera_context_framerate_propagation.cpp
                scene/camera/test_camera_trajectory_preserves_base_fields.cpp
                scene/camera/test_camera_stabilization.cpp
                scene/camera/golden_projection_test.cpp
                scene/camera/test_camera_near_plane_clip.cpp
                scene/camera/camera_25d_tests.cpp
                scene/camera/test_camera_hierarchy.cpp
                scene/camera/test_temporal_samples_pr1.cpp
                scene/camera/test_motion_blur_torture_pr1.cpp
                scene/test_scene_validator.cpp
                scene/test_shape_texture_fill.cpp
                scene/test_layer_order_contract.cpp
                scene/test_layer_kind.cpp
                scene/test_layer_content_ref.cpp
                scene/test_joints.cpp
                scene/test_motion_graph_prewarm.cpp
                render_graph/builder/test_graph_snapshot.cpp
)

chronon3d_add_test_suite(
    NAME   chronon3d_scene_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_SCENE_LINK_TARGETS}
    SOURCES ${SCENE_TEST_SOURCES}
    UNITY_EXCLUDE_SOURCES
        scene/camera/test_camera_program_compiled.cpp
        scene/camera/test_camera_program_compiled_metadata.cpp
        scene/camera/test_camera_program_compiled_trajectory.cpp
        scene/camera/test_camera_program_constraints_policy.cpp
        scene/camera/test_camera_program_orbit_ticket024.cpp
        scene/camera/test_camera_program_orbit_coherence_roll_parent.cpp
        scene/camera/test_camera_program_orientation_ticket022.cpp
        scene/camera/test_camera_program_projection_orientation.cpp
        scene/camera/test_shot_timeline_random_access.cpp
)
# TICKET-006: SCENE_TEXT_TESTS (test_layer_builder_animated, layout_design_kit,
# test_text_run_builder) call into chronon3d_text_core symbols whose definitions
# live in chronon3d_backend_text. Without this guard the linker errors with:
#   'undefined symbol: chronon3d::shape_resolved_run(...)'
#   'undefined symbol: chronon3d::text_run_materialize(...)'
# Mirrors the same guard in tests/core_tests.cmake. Both targets legitimately
# need backend_text because CORE_BLEND2D_TESTS and SCENE_TEXT_TESTS exercise
# different but overlapping text paths.
target_include_directories(chronon3d_scene_tests PRIVATE ${CMAKE_SOURCE_DIR})
if(TARGET chronon3d_camera_v1_legacy)
    target_sources(chronon3d_scene_tests PRIVATE $<TARGET_OBJECTS:chronon3d_camera_v1_legacy>)
endif()
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_backend_text)
endif()

# ── CAM-DOC 04 — 5 mandatory camera_v1 determinism test categories ──
# Compiled-execution / parity / golden struct hash / serial-vs-parallel /
# random-access determinism.  See tests/scene/camera/test_camera_compiled_evaluate.cpp.
chronon3d_add_test_suite(
    NAME   chronon3d_camera_compiled_evaluate_tests
    TIER   INTEGRATION
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_sdk_impl
        chronon3d_scene
    SOURCES scene/camera/test_camera_compiled_evaluate.cpp
)
target_include_directories(chronon3d_camera_compiled_evaluate_tests PRIVATE ${CMAKE_SOURCE_DIR})
if(TARGET chronon3d_camera_v1_legacy)
    target_sources(chronon3d_camera_compiled_evaluate_tests PRIVATE
        $<TARGET_OBJECTS:chronon3d_camera_v1_legacy>)
endif()

# Offline ChrononMotion adapter contract: baked scenes enter the canonical
# RenderPlan POD model before the renderer sees them; no motion solver runs in
# the frame path. The sibling source tree is an explicit test source include.
if(CHRONON3D_ENABLE_MOTION_ADAPTER)
    # UNIT-tier defaults to the canonical chronon3d_pipeline object/interface
    # boundary; the adapter is additive and remains an offline test dependency.
    chronon3d_add_test_suite(
        NAME chronon3d_motion_plan_adapter_tests
        TIER UNIT
        EXTRA_LINK_TARGETS chrononmotion_chronon3d_adapter
        SOURCES scene/motion/test_offline_motion_plan_adapter.cpp
    )
    target_include_directories(chronon3d_motion_plan_adapter_tests PRIVATE
        "${_chrononmotion_dir}/include")

endif()

# ── ADR-032 — contract-zone converters (chronon/abi.h motion handshake) ──────
# POD purity, version handshake, kind mapping, matrix packing round-trip,
# depth semantics, ContentRef extraction.
chronon3d_add_test_suite(
    NAME   chronon3d_motion_contract_bridge_tests
    TIER   UNIT
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_sdk_impl
        chronon3d_scene
    SOURCES scene/motion/test_motion_contract_bridge.cpp
            scene/motion/test_element_contract.cpp
)
target_include_directories(chronon3d_motion_contract_bridge_tests PRIVATE ${CMAKE_SOURCE_DIR})

# ── ADR-032 — motion-handshake determinism through the compiled camera path ──
# Serial vs parallel + random-access evaluation must produce bit-identical
# chronon_layer_state records (memcmp over the full POD).
chronon3d_add_test_suite(
    NAME   chronon3d_motion_contract_determinism_tests
    TIER   INTEGRATION
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_sdk_impl
        chronon3d_scene
    SOURCES scene/motion/test_motion_contract_determinism.cpp
)
target_include_directories(chronon3d_motion_contract_determinism_tests PRIVATE ${CMAKE_SOURCE_DIR})
