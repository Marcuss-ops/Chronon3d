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

# ── Probe: is chronon3d_text_core reachable via the SDK archive? ───
#
# The SDK installs <chronon3d/text/...> headers regardless of whether
# text_core .cpp files are compiled into the archive (text_core compile
# is gated by CHRONON3D_ENABLE_TEXT at SDK build time).  The probe
# builds a tiny executable that links against Chronon3D::SDK and calls
# a text_core-only symbol (`glyph_atlas_lookup`); the link resolves
# only when text_core is in the archive.
#
# Output is propagated to the consumer via the
# CHRONON3D_HAVE_TEXT_CORE compile definition (#define 0|1 in main.cpp).
# This lets main.cpp skip-or-include the explicit text-core node with
# a structured log line and ZERO link risk on text-off installs.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/probe_text.cpp" "
#include <chronon3d/text/glyph_atlas.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
int probe_text_core_main() {
    auto entry = chronon3d::glyph_atlas_lookup(
        std::string_view{}, std::uint32_t{0}, std::size_t{12});
    (void)entry;
    return 0;
}
int main(int, char**) { return probe_text_core_main(); }
")
try_compile(
    CHRONON3D_HAVE_TEXT_CORE
    "${CMAKE_CURRENT_BINARY_DIR}/text_probe"
    SOURCES "${CMAKE_CURRENT_BINARY_DIR}/probe_text.cpp"
    # Propagate CMAKE_PREFIX_PATH and CMAKE_TOOLCHAIN_FILE so the
    # probe inherits the build environment (vcpkg on dev machines).
    # Without CMAKE_TOOLCHAIN_FILE, Chronon3D::SDK would fail to link
    # on vcpkg-using systems because its transitive deps (fmt,
    # spdlog, tbb, ...) live behind the toolchain — the probe would
    # spuriously report CHRONON3D_HAVE_TEXT_CORE=0 even when text_core
    # is actually in the SDK archive.
    CMAKE_FLAGS
        "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        "-DCMAKE_CXX_STANDARD=20"
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    LINK_LIBRARIES Chronon3D::SDK
    OUTPUT_VARIABLE _chronon3d_text_probe_out
)
if(CHRONON3D_HAVE_TEXT_CORE)
    message(STATUS "check_install probe: text_core available via Chronon3D::SDK")
else()
    message(STATUS
        "check_install probe: text_core NOT available - explicit text-core node will be skipped. "
        "(Enable CHRONON3D_ENABLE_TEXT at SDK build to include text_core.)")
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

# Propagate the text_core probe result as a #define so main.cpp can
# conditionally include the explicit text-core probe node.
if(CHRONON3D_HAVE_TEXT_CORE)
    target_compile_definitions(check_install PRIVATE CHRONON3D_HAVE_TEXT_CORE=1)
else()
    target_compile_definitions(check_install PRIVATE CHRONON3D_HAVE_TEXT_CORE=0)
endif()
