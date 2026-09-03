# Core infrastructure test suites.
# Keep static registration here; feature-specific suites live in their domain manifests.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

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
