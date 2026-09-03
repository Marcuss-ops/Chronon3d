# tests/runtime/resource_state_tracker_tests.cmake
# New canonical resource-sync primitives (ResourceRange / UsageIntent /
# ResourceUse / ResourceStateResolver / ResourceTransition /
# ResourceStateTracker) + parallel comparison against the legacy
# BarrierPlan produced by GpuCommandPlanner.
chronon3d_add_test_suite(
    NAME chronon3d_resource_state_tracker_tests
    TIER UNIT
    SOURCES
        runtime/test_resource_state_tracker.cpp
)