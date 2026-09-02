# tests/runtime/async_encoder_sink_tests.cmake
# Bounded async encode queue + explicit drain lifecycle regression lock.
chronon3d_add_test_suite(
    NAME chronon3d_async_encoder_sink_tests
    TIER UNIT
    SOURCES runtime/test_async_encoder_sink.cpp
)
