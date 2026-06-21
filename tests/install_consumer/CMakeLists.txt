cmake_minimum_required(VERSION 3.25)

project(install_consumer
    VERSION 0.0.1
    LANGUAGES CXX
)

# ── End-to-end install boundary check (TICKET-011) ────────────────────
#
# This is a STANDALONE CMake project.  It uses ONLY what an installed
# Chronon3D package exposes.  It is intentionally NOT included in the
# in-tree tests/CMakeLists.txt: doing so would re-link against the
# in-tree targets and defeat the whole point of the test.
#
# The fact that find_package() resolves against a previously-installed
# `<prefix>` proves the install boundary produced a valid
# Chronon3DConfig.cmake + Chronon3DTargets.cmake package.

find_package(Chronon3D CONFIG REQUIRED)

# Pre-condition: the documented public API surface must be intact.
# If the install script ever strips these targets, this fails fast
# with a clear error instead of producing a vague link error further
# downstream.  The PUBLIC SURFACE contract is defined in the
# top-level CMakeLists.txt (the block headed "PUBLIC SURFACE
# contract (TICKET-011 cmake-boundary)").
if(NOT TARGET Chronon3D::SDK)
    message(FATAL_ERROR
        "Install boundary broken: Chronon3D::SDK target absent. "
        "The PUBLIC SURFACE contract (`the documented public API "
        "surface is exactly: Chronon3D::SDK`) defined in the top-level "
        "CMakeLists.txt is not satisfied by the current install.")
endif()

add_executable(check_install main.cpp)

# C++20 — must match the SDK's compile feature set so the consumer's
# headers behave the same way as in-tree.
set_target_properties(check_install PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Single PUBLIC-API link call.  chronon3d_sdk chronon3d_sdk_impl propagates
# transitively through Chronon3D::SDK at link time.
target_link_libraries(check_install PRIVATE Chronon3D::SDK)
