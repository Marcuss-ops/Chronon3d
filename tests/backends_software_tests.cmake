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
    # 06 R5b follow-up regression guard — locks real SoftwareRenderer&&
    # move semantics so the EAST-CONST const&& trick cannot regress.
    backends/software/test_software_renderer_move.cpp
    # TICKET-070 — Fase 1#10 close-out regression for the validation-gate
    # factory make_software_backend(SoftwareBackendServices).  Exercises
    # the canonical assert() guards (debug) AND the structured
    # Result::err release path (NDEBUG) for every REQUIRED service:
    # counters, settings, framebuffer_pool, asset_resolver,
    # text_resources, owner.  Static-grep on
    # `src/backends/software/software_backend.cpp` is the defence-in-depth
    # layer that survives both build modes.  See test file header for the
    # full design rationale + test_font_io_fence.cpp pattern precedent.
    backends/software/test_software_backend_factory.cpp
    # M1.5#6 — four-stage text_run_processor split regression lock.
    # These tests verify the internal contract of the text scratch pool
    # (NO vector realloc per draw — strict user spec), the FNV-1a hash
    # determinism of the raster-stage output, and the CHRONON3D_TEXT_BENCH_PARALLEL
    # env-var toggle (serial vs parallel mode produces identical output).
    # NO font fixture required; probes TextScratchManager directly via
    # the rctx.text_resources ABI (same pointer type the production hot
    # path uses).  Headers include text_run_processor.hpp but the tests
    # do NOT call draw_text_run — so no expanded link dependency needed.
    backends/software/test_text_run_processor_scratch_pool.cpp
    backends/software/test_text_run_processor_golden_raster.cpp
    backends/software/test_text_run_processor_bench_serial_vs_parallel.cpp
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
