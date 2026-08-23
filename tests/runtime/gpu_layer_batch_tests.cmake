# tests/runtime/gpu_layer_batch_tests.cmake
# Fase F (TICKET-VIDEO-COMPILER-ARCH-V1) — GpuLayerBatch UNIT test.
chronon3d_add_test_suite(
    NAME chronon3d_gpu_layer_batch_tests
    TIER UNIT
    SOURCES
        runtime/test_gpu_layer_batch.cpp
        runtime/test_device_scheduler.cpp
)