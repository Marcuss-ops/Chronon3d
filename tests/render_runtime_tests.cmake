# Render/runtime regression suites.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_compositor_tests
    TIER INTEGRATION
    EXTRA_LINK_TARGETS chronon3d_scene
    SOURCES
        render_graph/pipeline/test_composite_origin_regression.cpp
        compositor/test_blend_reference.cpp
        compositor/test_blend_simd_equivalence.cpp
        compositor/test_track_matte.cpp
)
target_compile_definitions(chronon3d_compositor_tests PRIVATE
    CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(TARGET chronon3d::content)
    target_link_libraries(chronon3d_compositor_tests PRIVATE chronon3d::content)
endif()

if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    chronon3d_add_test_suite(
        NAME chronon3d_io_tests
        TIER SDK
        EXTRA_LINK_TARGETS
            chronon3d_sdk_impl
            chronon3d_pipeline
            chronon3d_backend_image
        SOURCES
            io/test_image_writer.cpp
            io/test_png_validity.cpp
            io/test_image_writer_throw.cpp
    )
    if(CHRONON3D_ENABLE_EXR)
        target_sources(chronon3d_io_tests PRIVATE io/test_exr_writer.cpp)
    endif()

    chronon3d_add_test_suite(
        NAME chronon3d_tbb_workers_test
        TIER INTEGRATION
        EXTRA_LINK_TARGETS chronon3d_scene
        SOURCES render_graph/executor/test_tbb_workers_parallelism.cpp
    )
    target_compile_definitions(chronon3d_tbb_workers_test PRIVATE
        CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    chronon3d_add_test_suite(
        NAME chronon3d_deterministic_tests
        TIER INTEGRATION
        EXTRA_LINK_TARGETS
            chronon3d_graph
            chronon3d_graph_pipeline
            chronon3d_scene
        SOURCES
            deterministic/test_deterministic.cpp
            deterministic/test_determinism_harness.cpp
            deterministic/gradient_determinism_tests.cpp
            deterministic/test_baseline_green.cpp
            deterministic/test_tile_determinism.cpp
            render_graph/executor/test_scheduler_determinism.cpp
            deterministic/test_visual_regression_scenarios.cpp
            deterministic/test_determinism_matrix.cpp
            deterministic/test_sequential_graph_cache.cpp
            deterministic/test_brute_determinism.cpp
    )
    target_compile_definitions(chronon3d_deterministic_tests PRIVATE
        CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    if(TARGET chronon3d::content)
        target_link_libraries(chronon3d_deterministic_tests PRIVATE chronon3d::content)
    endif()
endif()

# One precomp target is shared by fast and render aggregates. It keeps the
# original Blend2D-only availability without compiling the same source twice.
if(CHRONON3D_USE_BLEND2D)
    chronon3d_add_test_suite(
        NAME chronon3d_precomp_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software
        SOURCES render_graph/nodes/test_precomp_node_cache.cpp
    )
    target_compile_definitions(chronon3d_precomp_tests PRIVATE
        CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    if(TARGET chronon3d::content)
        target_link_libraries(chronon3d_precomp_tests PRIVATE chronon3d::content)
    endif()
endif()

# Render-graph compiler contracts.
chronon3d_add_test_suite(
    NAME chronon3d_fusion_pass_tests
    TIER UNIT
    SOURCES render_graph/compiler/test_fusion_pass.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_template_program_tests
    TIER UNIT
    SOURCES render_graph/compiler/test_template_program.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_segment_execution_tests
    TIER UNIT
    SOURCES render_graph/compiler/test_segment_execution_descriptor.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_glow_fullframe_audit_tests
    TIER UNIT
    SOURCES render_graph/pipeline/test_glow_fullframe_audit.cpp
)

# Runtime/compiler execution contracts.
chronon3d_add_test_suite(
    NAME chronon3d_template_program_cache_tests
    TIER UNIT
    SOURCES runtime/test_template_program_cache.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_gpu_layer_batch_tests
    TIER UNIT
    SOURCES
        runtime/test_gpu_layer_batch.cpp
        runtime/test_device_scheduler.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_gpu_command_plan_tests
    TIER UNIT
    SOURCES runtime/test_gpu_command_plan.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_resource_state_tracker_tests
    TIER UNIT
    SOURCES runtime/test_resource_state_tracker.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_compiled_resource_authority_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_pipeline
    SOURCES runtime/test_compiled_resource_authority.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_async_encoder_sink_tests
    TIER UNIT
    SOURCES runtime/test_async_encoder_sink.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_semantic_core_tests
    TIER UNIT
    SOURCES runtime/test_semantic_core.cpp
)
