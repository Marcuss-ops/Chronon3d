cmake_minimum_required(VERSION 3.25)

project(install_consumer
    VERSION 0.0.1
    LANGUAGES CXX
)

# -------------------------------------------------------------------------
# End-to-end install boundary check (TICKET-011 + P3-5 strict-A)
#
# This is a STANDALONE CMake project.  It uses ONLY what an installed
# Chronon3D package exposes.  It is intentionally NOT included in the
# in-tree tests/CMakeLists.txt: doing so would re-link against the
# in-tree targets and defeat the whole point of the test.
#
# The fact that find_package() resolves against a previously-installed
# <prefix> proves the install boundary produced a valid
# Chronon3DConfig.cmake + Chronon3DTargets.cmake package.
#
# P3-5 — strict-A "manifest-only API surface":
#   This consumer transits ONLY the V0.1 public-header manifest
#   (cmake/Chronon3DPublicHeaders.cmake).  The DIRECT #include set in
#   main.cpp is exactly:
#     - <chronon3d/chronon3d.hpp>           (umbrella, manifest entry)
#   Transitive include reachability from the umbrella is documented in
#   docs/adr/ADR-012-chronon3d-sdk-manifest-boundary.md as "manifest
#   scope" (FrameContext / Scene / SceneFunction / CompositionSpec /
#   RenderEngine / save_png / Framebuffer are reachable, NOT creep).
#   The consumer's runtime calls are strictly:
#     * chronon3d::RenderEngine::render(const Composition&, Frame)
#     * chronon3d::save_png(*fb, path)
#   Plus CompositionSpec + SceneFn + Scene default-construction to give
#   the engine something to render.  No SceneBuilder / LayerBuilder /
#   active camera / non-empty SceneFn.
#
#   Per the user's explicit choice ("Strict (A) rewrite"), the seal-
#   discussion surface was DOWNSCOPED from "renders a meaningful test
#   PNG with text+grid+camera" (Fase-6 spec, 2026-06-23) to "demonstrate
#   that an installed SDK can be linked, a render invoked, and a PNG
#   emitted to disk."  An empty SceneFn yields a near-empty / all-black
#   framebuffer by design; the prior kMinNonZeroPixels=1000 threshold
#   was REMOVED for this strict-A scope.  See ADR-012 for the deferred
#   remediation of the OPP-side manifest leak that the empty SceneFn
#   works around.
#
#   The OPP-side CMP0077 INTERFACE_INCLUDE_DIRECTORIES source-tree path
#   leak (deferred Commit C from Step 3) is INDEPENDENT of the consumer's
#   surface discipline asserted here; gate 10 still FAILs upstream of
#   this consumer's build on that leak.  Both are tracked separately.
# -------------------------------------------------------------------------
find_package(Chronon3D CONFIG REQUIRED)

# Pre-condition: the documented public API surface must be intact.
# If the install script ever strips these targets, this fails fast
# with a clear error instead of producing a vague link error further
# downstream.  The PUBLIC SURFACE contract is defined in
# `cmake/Chronon3DPublicHeaders.cmake` (single source of truth for
# the V0.1 SDK manifest) and propagated through Chronon3DSdkTargets.cmake.
if(NOT TARGET Chronon3D::SDK)
    message(FATAL_ERROR
        "Install boundary broken: Chronon3D::SDK target absent. "
        "The V0.1 public-header manifest (cmake/Chronon3DPublicHeaders.cmake) "
        "and the PUBLIC SURFACE contract defined in cmake/Chronon3DSdkTargets.cmake "
        "are not satisfied by the current install.")
endif()

add_executable(check_install main.cpp)

# C++20 — must match the SDK's compile feature set so the consumer's
# headers behave the same way as in-tree.
set_target_properties(check_install PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Single PUBLIC-API link call.  chronon3d_sdk_impl propagates
# transitively through Chronon3D::SDK at link time.
target_link_libraries(check_install PRIVATE Chronon3D::SDK)
