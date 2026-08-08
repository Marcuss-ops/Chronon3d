chronon3d_add_test_suite(
    NAME chronon3d_memory_tests
    TIER UNIT
    LINK_TARGETS chronon3d_pipeline
    SOURCES
        perf/test_node_memory_counters_v1.cpp
        perf/test_node_memory_tracker.cpp
)
