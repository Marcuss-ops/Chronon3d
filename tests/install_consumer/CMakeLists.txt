cmake_minimum_required(VERSION 3.25)

project(install_consumer
    VERSION 0.0.1
    LANGUAGES CXX
)

# -------------------------------------------------------------------------
# End-to-end install boundary check (TICKET-011 + P3-H)
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
# P3-H — manifest-clean V0.1 surface:
#   This consumer transits ONLY the V0.1 public-header manifest
#   (cmake/Chronon3DPublicHeaders.cmake).  The DIRECT #include set in
#   main.cpp is exactly:
#     - <chronon3d/chronon3d.hpp>           (umbrella, manifest entry)
#   Transitive includes from the umbrella (math/*, animation/*,
#   geometry/*, timeline/composition.hpp, ...) ARE part of manifest
#   scope: the umbrella header cannot expose its public vocabulary
#   without them.  This is documented in docs/RELEASE_GATE.md under
#   ## SDK Product V1 (item 8).
#   The consumer does NOT reach — directly or via try_compile probe —
#   for any OPP-internal / advanced / test header.
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

# -------------------------------------------------------------------------
# P3-H — text_core coverage is no longer asserted from the consumer side.
# The previous `try_compile` probe used `<chronon3d/text/glyph_atlas.hpp>`
# (NOT in the V0.1 manifest).  text_core inclusion in the shipped SDK
# archive is now exclusively verified by
# `tools/sdk/check_archive_canaries.sh` (Phase 3 of the
# install_consumer_test.sh pipeline), where the canary symbol table is
# matched against the OPP's `cmake/Chronon3DCanarySymbols.cmake`.
# -------------------------------------------------------------------------
