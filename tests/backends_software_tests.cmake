# tests/backends_software_tests.cmake
#
# 06 R2 — backend / processor-context acceptance test bundle.
#
# Lives behind `CHRONON3D_BUILD_TESTS` (the project's own flag, consistent with
# the per-area .cmake pattern). The test source itself (`tests/backends/software/
# test_software_processor_context.cpp`) is wrapped in `#if defined(
# CHRONON3D_BUILD_TESTS)` so it is a no-op when the flag is OFF.
#
# This bundle currently contains exactly one file:
#   * `test_software_processor_context.cpp`
#     Asserts the header-only POD wires default to all-null pointers and that
#     a copy of a default-constructed context preserves the null state. It
#     does NOT exercise the full render pipeline (build cost + no Content
#     dependency). Future R3b/R4 backend migration acceptance tests should
#     land in this same target.
#
# Note on dependencies: we link against `chronon3d_sdk` and
# `chronon3d_sdk_impl`, NOT `chronon3d_backends_software` directly — the
# header-only POD lives in the public `chronon3d_sdk` and the
# `make_processor_context(SoftwareRenderer* renderer)` helper symbol is
# exported by `chronon3d_sdk_impl`. This keeps the test build independent
# of a full renderer TLB pull.

add_executable(chronon3d_backends_software_tests
    ${TEST_MAIN}
    backends/software/test_software_processor_context.cpp
)

target_link_libraries(chronon3d_backends_software_tests PRIVATE
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    doctest::doctest
)

target_include_directories(chronon3d_backends_software_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests
)

chronon3d_enable_test_pch(chronon3d_backends_software_tests)

add_test(
    NAME chronon3d_backends_software_tests
    COMMAND chronon3d_backends_software_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
