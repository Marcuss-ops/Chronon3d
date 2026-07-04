# tests/debug/CMakeLists.txt — diagnostic test bundle
#
# This directory hosts minimal-repro debug targets that are intentionally
# NOT shipped with the per-area test bundles (which are organized around
# the test conventions of tests/{backends_software,core,scene,text,...}.cmake).
#
# Each diagnostic here:
#   - is gated by CHRONON3D_BUILD_TESTS (same as the per-area bundles);
#   - uses the CANONICAL in-tree test link contract (mirrors
#     `tests/io_tests.cmake`'s `chronon3d_io_tests`): chronon3d_sdk
#     INTERFACE + chronon3d_sdk_impl STATIC archive + chronon3d_pipeline
#     INTERFACE + chronon3d_backend_image OBJECT;
#   - adds ${CMAKE_SOURCE_DIR} + ${Stb_INCLUDE_DIR} explicitly so that
#     `<stb_image.h>` (consumed header-only) resolves — the Stb dep is
#     marked PRIVATE on chronon3d_backend_image and does not propagate;
#   - bypasses the `chronon3d_add_test_suite` helper specifically to avoid
#     the gtest test_main.cpp auto-injection (these are diagnostic
#     binaries, not CI-visible tests; they only need to be runnable on
#     demand).
#
# Each diagnostic test prints pixel-level evidence to stdout so the
# READ vs WRITE symmetry can be inspected manually.

if(CHRONON3D_BUILD_TESTS)

add_executable(chronon3d_save_png_roundtrip_test
    debug/test_save_png_roundtrip.cpp
)
# M1.5#8 / TICKET-GATE-10-PHASE-4-BLACK — canonical in-tree test link
# contract (post-fix).  Mirrors tests/io_tests.cmake's chronon3d_io_tests
# pattern (which links + runs + asserts pixel-level output in CI).
# Iteration history:
#   - `chronon3d_pipeline` only  → undefined RendererBufferRing, SoftwareRenderer, etc.
#   - `chronon3d` + `chronon3d_backend_image` → undefined g_current_counters, simd::clear_framebuffer
#   - canonical full chain + Stb_INCLUDE_DIR → expected to compile + link
target_link_libraries(chronon3d_save_png_roundtrip_test
    PRIVATE
        chronon3d_sdk           # INTERFACE: includes + transitive link deps
        chronon3d_sdk_impl      # STATIC archive: aggregate of every OBJECT lib
        chronon3d_pipeline      # INTERFACE: propagate engine-wide link closure
        chronon3d_backend_image # OBJECT: stb_image_backend + image_writer symbols
)
target_include_directories(chronon3d_save_png_roundtrip_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}     # project-internal headers (e.g. include/chronon3d/...)
        ${Stb_INCLUDE_DIR}      # <stb_image.h> resolves here (Stb::Stb dep is PRIVATE on chronon3d_backend_image, not propagated)
)
# Standalone diagnostic — not registered as a CTest test (avoids the
# reinstall/test aggregation).  Run on demand via
# `<build>/tests/chronon3d_save_png_roundtrip_test`.

endif()  # CHRONON3D_BUILD_TESTS
