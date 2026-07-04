# ==============================================================================
# cmake/Chronon3DVcpkgToolchain.cmake
#
# Chronon3d — Canonical vcpkg toolchain wrapper (SINGLE SOURCE OF TRUTH)
# ==============================================================================
# This file is the value of `CMAKE_TOOLCHAIN_FILE` in every CMakePresets entry
# (inherited transparently via `inherits: base-linux`).  All presets that
# previously hardcoded `vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake` or
# `${sourceDir}/vcpkg_installed/...` paths now go through this wrapper, which
# exports two things:
#
#   1. CHRONON3D_VCPKG_TOOLCHAIN_FILE  (cache FILEPATH)
#      → canonical absolute path of the real `vcpkg.cmake` script.  Any CMake
#        code (or downstream CMakePresets override) that needs the canonical
#        toolchain path reads this variable.  Drift-safe: there is EXACTLY
#        ONE place in this repository that names the vcpkg toolchain.
#
#   2. CMAKE_PREFIX_PATH (list, augmented)
#      → prepends `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}` so
#        `find_package()` (config-mode) can locate vcpkg-installed packages
#        released under the current triplet — including release and debug
#        subdirs, which CMake auto-traverses based on CMAKE_BUILD_TYPE.
#        Previously hardcoded twice as
#           `${sourceDir}/vcpkg_installed/${presetName}/x64-linux/debug;${sourceDir}/vcpkg_installed/${presetName}/x64-linux`
#        — two duplicate copies of the same string.  Both removed; this
#        wrapper is the only place that publishes the prefix path.
#
# The wrapper then `include()`s the canonical vcpkg toolchain so it remains
# a fully-functional toolchain file (vcpkg.cmake performs all compiler /
# link-flag setup that the build actually depends on).
#
# INVARIANTS (enforced by tools/check_vcpkg_canonical_path.sh):
#   I1.  Every preset JSON sets CMAKE_TOOLCHAIN_FILE to the SAME single path
#        (this wrapper), never to `vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake`
#        directly.
#   I2.  No preset JSON hardcodes a `vcpkg_installed/...` substring — the
#        prefix path is owned by this wrapper.
#   I3.  This file declares the cache variable CHRONON3D_VCPKG_TOOLCHAIN_FILE
#        and `include()`s that exact path at the bottom.
#   I4.  Any CMake script that needs the vcpkg toolchain path reads
#        `${CHRONON3D_VCPKG_TOOLCHAIN_FILE}` — never re-derives the path
#        from `${CMAKE_SOURCE_DIR}/vcpkg_bootstrap/...`.
# ==============================================================================

# Canonical path: this file lives at `<project>/cmake/`.  The canonical
# vcpkg toolchain is at `<project>/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake`.
set(CHRONON3D_VCPKG_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
    CACHE FILEPATH
    "Chronon3d canonical vcpkg toolchain path. Single source of truth for all CMakePresets entries — DO NOT reference vcpkg_bootstrap/... or vcpkg_installed/... directly in preset JSONs.")

# Sanity check: fail-fast at configure time if the canonical path is wrong
# (e.g. vcpkg_bootstrap/ was moved and this wrapper was not updated).
# Without this guard, the include() at the bottom silently produces a
# confusing vcpkg error deep in find_package() calls.
if(NOT EXISTS "${CHRONON3D_VCPKG_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "CHRONON3D_VCPKG_TOOLCHAIN_FILE points to a non-existent path:\n"
        "  ${CHRONON3D_VCPKG_TOOLCHAIN_FILE}\n"
        "Either restore the vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake "
        "artifact or update the canonical path in cmake/Chronon3DVcpkgToolchain.cmake.")
endif()

# Consolidate `CMAKE_PREFIX_PATH` so find_package() can locate vcpkg-installed
# packages for the current triplet without each preset JSON having to duplicate
# the substring.  This replaces the two prior duplicates:
#   • cmake/presets/linux-fast-dev.json         (preset "linux-fast-dev")
#   • cmake/presets/development.json           (preset "linux-ci-lean-gate")
# CMake's config-mode find_package() auto-traverses Release/ and Debug/
# subdirs under the triplet root based on CMAKE_BUILD_TYPE — no need to
# enumerate both variants.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()

# Delegate to the real vcpkg toolchain.  vcpkg.cmake does all compiler/linker
# setup the build depends on (host → target cross-compilation, manifests,
# installed-dir triplet layout).  Including it from this wrapper means the
# wrapper IS a toolchain file (preserves the CMAKE_TOOLCHAIN_FILE contract
# while adding our presets-wide invariants).
include("${CHRONON3D_VCPKG_TOOLCHAIN_FILE}")
