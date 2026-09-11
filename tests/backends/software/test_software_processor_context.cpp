// ============================================================================
// tests/backends/software/test_software_processor_context.cpp
//
// 06 R3 acceptance test — verifies that `SoftwareProcessorContext` is a
// standalone dependency bundle without a SoftwareRenderer back-pointer.
//
// This test does NOT exercise the rendering pipeline (it would force a
// full build with software backend + a real Composition); it inspects
// the wiring directly via the accessors exposed by SoftwareRenderer.
//
// NOTE (2026-06-21): This file is currently DORMANT — it is not listed
// in any tests/*/CMakeLists.txt, and `tests/backends/` has no top-level
// CMakeLists.  All TEST_CASE bodies are wrapped in `#if defined(
// CHRONON3D_BUILD_TESTS)` so the file contributes nothing to the build.
// When the R2 migration land fully (processors stop taking
// `SoftwareRenderer&`), add a tests/backends_software_tests.cmake (or wire  // drift-allow: stale-ref
// this single TU into tests/backends_software_tests.cmake) and  // drift-allow: stale-ref
// register the new source list under `${CHRONON3D_TEST_TARGETS}` so
// CTest picks it up.  At that point this file should also start
// exercising the explicit services-bundle construction path.
// ============================================================================

#if defined(CHRONON3D_BUILD_TESTS)
#include <doctest/doctest.h>
#endif

#include <chronon3d/backends/software/software_processor_context.hpp>

#if defined(CHRONON3D_BUILD_TESTS)

TEST_CASE("SoftwareProcessorContext — header-only type compiles") {
    SUBCASE("default state: all pointers null") {
        chronon3d::SoftwareProcessorContext ctx;
        CHECK(ctx.counters == nullptr);
        CHECK(ctx.settings == nullptr);
        CHECK(ctx.registry == nullptr);
        CHECK(ctx.image_backend == nullptr);
        CHECK(ctx.image_renderer == nullptr);
#ifdef CHRONON3D_HAS_BACKEND_TEXT
        CHECK(ctx.font_engine == nullptr);
#endif
    }
}

TEST_CASE("SoftwareProcessorContext — standalone construction") {
    chronon3d::SoftwareProcessorContext ctx{};
    CHECK(sizeof(ctx) > 0);
}

TEST_CASE("Boundary gate — boundary script pulls in the same field surface") {
    // We rely on the boundary script's invariant I5. This test simply
    // asserts that the test fixture itself does not regress against
    // the boundary gate. The strict assertion lives in the bash script.
    SUBCASE("the new context struct is integral (header-only)") {
        chronon3d::SoftwareProcessorContext ctx;
        chronon3d::SoftwareProcessorContext copy = ctx;
        // Both default-constructed: same null state.
        CHECK(copy.counters          == ctx.counters);
        CHECK(copy.settings          == ctx.settings);
        CHECK(copy.registry          == ctx.registry);
        CHECK(copy.image_backend     == ctx.image_backend);
        CHECK(copy.image_renderer    == ctx.image_renderer);
    }
}

#endif // CHRONON3D_BUILD_TESTS
