# tests/runtime/gpu_command_plan_tests.cmake
chronon3d_add_test_suite(
    NAME chronon3d_gpu_command_plan_tests
    TIER UNIT
    SOURCES
        runtime/test_gpu_command_plan.cpp
)
