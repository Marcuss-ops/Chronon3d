# Core test-suite orchestrator. Source ownership lives in manifests/.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
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
