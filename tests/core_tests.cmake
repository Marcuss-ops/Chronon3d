# Core test-suite orchestrator. Source ownership lives in manifests/.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

# Static architecture invariants are executed through the canonical declarative
# registry. Procedural SDK shell syntax remains an independent CTest.
add_test(
    NAME chronon3d_architecture_registry
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_architecture.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_architecture_registry PROPERTIES
    LABELS "architecture;gate"
)
add_test(
    NAME chronon3d_asset_consumer_shell_syntax
    COMMAND bash -n ${CMAKE_SOURCE_DIR}/tools/sdk/run_asset_authoring_consumer.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(chronon3d_asset_consumer_shell_syntax PROPERTIES
    LABELS "architecture;assets;sdk;gate"
)

if(NOT CHRONON3D_ENABLE_MESH)
    chronon3d_add_test_suite(
        NAME chronon3d_mesh_disabled_smoke
        TIER UNIT
        SOURCES assets/mesh_disabled_smoke.cpp
    )
endif()

include(${CMAKE_CURRENT_LIST_DIR}/manifests/core_text_sources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/manifests/core_general_sources.cmake)

chronon3d_add_test_suite(
    NAME chronon3d_core_tests
    TIER INTEGRATION
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_sdk_impl
        chronon3d_pipeline
        chronon3d_runtime
    SOURCES ${CORE_TEST_SOURCES}
)

if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_sources(chronon3d_core_tests PRIVATE
        $<TARGET_OBJECTS:chronon3d_text_core>
        $<TARGET_OBJECTS:chronon3d_backend_text>
    )
endif()

target_compile_definitions(chronon3d_core_tests PRIVATE
    CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})

# Core infrastructure suites live here too; they do not need a parallel
# one-suite-per-file manifest layer.
chronon3d_add_test_suite(
    NAME chronon3d_backend_registry_tests
    TIER UNIT
    SOURCES registry/test_backend_registry.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_memory_tests
    TIER UNIT
    SOURCES
        perf/test_node_memory_counters_v1.cpp
        perf/test_node_memory_tracker.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_optimizer_tests
    TIER UNIT
    SOURCES render_graph/optimizer/test_graph_optimizer.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_preflight_tests
    TIER UNIT
    SOURCES preflight/test_path_existence_map.cpp
)

# Cache suites are core runtime infrastructure and share the same registration
# authority instead of a one-file wrapper.
chronon3d_add_test_suite(
    NAME chronon3d_cache_tests
    TIER UNIT
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_graph_cache
    SOURCES
        cache/test_cache_diagnostics.cpp
        cache/test_cache_policy.cpp
        cache/test_persistent_framebuffer_store.cpp
        cache/test_lru_cache.cpp
        cache/test_native_video_decoder_lru.cpp
        cache/test_lru_extensions.cpp
        cache/test_frame_cache.cpp
        cache/test_evict_lru_for.cpp
        cache/test_video_frame_cache.cpp
        cache/test_framebuffer_pool.cpp
        cache/test_node_cache_hash_includes_camera.cpp
        cache/test_node_cache_identity_builder.cpp
        cache/test_node_cache.cpp
        cache/test_node_cache_ae_sweep.cpp
        render_graph/cache/test_scene_program_cache.cpp
        cache/stress/test_cache_diagnostics_stress.cpp
        cache/stress/test_camera_transition_catalog_stress.cpp
        render_graph/cache/test_compiled_graph_cache.cpp
        cache/test_cache_reuse_identical_frame.cpp
        cache/test_cache_invariance.cpp
)
if(CHRONON3D_ENABLE_VIDEO)
    target_sources(chronon3d_cache_tests PRIVATE cache/test_hash_builder.cpp)
endif()
chronon3d_add_test_suite(
    NAME chronon3d_parse_framebuffer_pool_clear_policy_tests
    TIER UNIT
    SOURCES cache/test_parse_framebuffer_pool_clear_policy.cpp
)

# Optional OCIO coverage belongs to the core color/math domain.
if(CHRONON3D_ENABLE_OCIO)
    chronon3d_add_test_suite(
        NAME chronon3d_ocio_tests
        TIER UNIT
        LINK_TARGETS
            chronon3d_core_impl
            OpenColorIO::OpenColorIO
        SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/core/math/test_ocio_color_transform.cpp
    )
endif()
