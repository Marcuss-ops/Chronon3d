# ═══════════════════════════════════════════════════════════════════════════
# Chronon3D — Content Registry Consumer Example
#
# Minimal standalone project demonstrating external consumption of the
# canonical content registry through the public SDK target.
#
# ── Option A: consume an installed Chronon3D package ──────────────────────
#   cmake .. -DCMAKE_PREFIX_PATH=/path/to/chronon3d-install
# ═══════════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.27)
project(content_registry_consumer VERSION 1.0.0 LANGUAGES CXX)

# ── Option A: installed package ────────────────────────────────────────────
find_package(Chronon3D CONFIG REQUIRED)

# ── Option B: same build tree (Chronon3d vendored in this workspace) ─────
#   Replace the find_package line above with:
#
#     add_subdirectory(external/Chronon3d)
#
#   The in-tree build defines the same Chronon3D::SDK alias target, so the
#   target_link_libraries below works unchanged for either path.

if(NOT TARGET Chronon3D::SDK)
    message(FATAL_ERROR
        "Chronon3D::SDK target not found. "
        "Ensure CMAKE_PREFIX_PATH points to a valid Chronon3D install prefix "
        "(or use the add_subdirectory path above).")
endif()

add_executable(content_registry_consumer main.cpp)

set_target_properties(content_registry_consumer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Link ONLY the public SDK target.
target_link_libraries(content_registry_consumer PRIVATE Chronon3D::SDK)
