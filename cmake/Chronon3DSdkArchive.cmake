# ==============================================================================
# cmake/Chronon3DSdkArchive.cmake — SDK archive merge mechanics
#
# PURPOSE
#   Single source of truth for the *archive* layer of the SDK:
#     • Build-time manifest computation for `libchronon3d_sdk_impl.a`
#       (registry-driven GLOB_RECURSE filter + stale-manifest guard).
#     • `sdk_archive_merge` POST_BUILD custom target that calls `ar crs`
#       (response-file-driven, see `sdk_archive_merge.cmake`) to concatenate
#       every per-subsystem .o file into the single aggregated archive.
#     • `install(CODE …)` hook that wires the merge target into
#       `cmake --install` so a fresh consumer install always sees a
#       populated archive, not the marker-only stub that CMake 3.25 emits
#       by default for STATIC libraries whose only file is a marker TU.
#     • Reference to the canonical canary catalog at
#       `cmake/Chronon3DCanarySymbols.cmake` (consumed by
#       tools/install_consumer_test.sh's Fase-4 verification gate; lives
#       next to this file because its lifecycle is the same: per-area
#       symbol list + signature invariants).
#
# PRECONDITIONS  (enforced by the caller, cmake/Chronon3DSdkTargets.cmake)
#   • cmake/Chronon3DRegistry.cmake has been included (CHRONON3D_REGISTRY_OBJECT_LIBS).
#   • The `chronon3d_sdk_impl` STATIC target has been declared by the
#     caller.  This module appends only the manifest / merge logic to it.
#
# INCLUDED FROM
#   cmake/Chronon3DSdkTargets.cmake — after `add_library(chronon3d_sdk_impl …)`
#   and the registry-driven foreach target_link_libraries block.
#
# CONTRACT  (TICKET-011 cmake-boundary — AGGREGATED ARCHIVE)
#   CMake 3.25 does NOT embed per-subsystem .o files into a STATIC
#   archive emitted via `target_link_libraries(STATIC PRIVATE objlib)`.
#   The empirical observation that allows this workaround to be precise
#   is that the produced .a contains only the marker TU (1 .o file,
#   ~3 KB).  The .o propagation happens at final link time, but the
#   intermediate archive is empty.
#   WORKAROUND: `sdk_archive_merge` rebuilds the archive from scratch
#   (`ar crs`, response-file invocation) using the deterministic
#   manifest computed below.  Upgrade to CMake ≥3.28 to retire this
#   workaround (native OBJECT aggregation into STATIC is fixed upstream).
# ==============================================================================

# ── Optional sanity: the canary catalog is consumed at install time by
#    tools/install_consumer_test.sh.  Verify the catalog file is present
#    at configure time so a missing file is surfaced loud before CI runs.
#    The catalog IS NOT consumed by configure-time logic; this check is
#    defensive documentation only.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake/Chronon3DCanarySymbols.cmake")
    message(WARNING
        "Chronon3DSdkArchive: canary catalog missing at "
        "${CMAKE_SOURCE_DIR}/cmake/Chronon3DCanarySymbols.cmake. "
        "tools/install_consumer_test.sh Fase-4 gate will fail loud.")
endif()

# ── Generator compatibility gate ─────────────────────────────────────
# Whole-tree GLOB_RECURSE picks up per-subsystem .cpp.o files at
# paths like `<build>/**/CMakeFiles/<target>.dir/*.cpp.o`.  This layout
# is guaranteed by the Ninja generator; multi-config generators
# (Visual Studio, XCode) use a different intermediate-dir layout that
# this script does NOT enumerate.  Surface the limit early so a
# generator flip is detected at configure time, not at first build.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR
        "sdk_archive_merge: registry-driven manifest assumes Ninja 'CMakeFiles/<target>.dir/*.cpp.o' intermediate-dir layout "
        "(have generator '${CMAKE_GENERATOR}').")
endif()

# ── Deterministic object manifest ────────────────────────────────────────────
# Two-step enumeration keeps the .o list reproducible:
#
#   1) Whole-tree scan restricted to the same paths the original
#      implementation used:
#        file(GLOB_RECURSE ${CMAKE_BINARY_DIR}/src/*.cpp.o ${BUILD_DIR}/content/*.cpp.o)
#      A recursive glob from `${CMAKE_BINARY_DIR}/*.cpp.o` alone is fragile
#      across CMake versions; the two explicit patterns are known-good.
#
#   2) Registry filter: keep entries whose path contains
#      `/<reg_target>.dir/` for any target in CHRONON3D_REGISTRY_OBJECT_LIBS
#      plus chronon3d_sdk_impl (the marker).  Adding a new OBJECT lib to the
#      registry is therefore the single line of maintenance needed.
#
# Trade-off documented in commit history; supports unity-build=OFF
# (`linux-ci` / install-boundary CI).  The merge script handles
# unity-build=ON gracefully via `if(EXISTS …)`.
file(GLOB_RECURSE _all_cpp_o
    "${CMAKE_BINARY_DIR}/src/*.cpp.o"
    "${CMAKE_BINARY_DIR}/content/*.cpp.o"
)

set(_sdk_archive_obj_files "")
foreach(_obj IN LISTS _all_cpp_o)
    set(_keep FALSE)
    foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
        string(FIND "${_obj}" "/${_reg_obj}.dir/" _hit)
        if(_hit GREATER -1)
            set(_keep TRUE)
            break()
        endif()
    endforeach()
    if(_keep)
        list(APPEND _sdk_archive_obj_files "${_obj}")
    endif()
endforeach()

# Marker TU: chronon3d_sdk_impl is a STATIC library (not in OBJECT_LIBS),
# but its single source produces a per-source .o under the same Ninja
# layout — at
#   <build>/src/CMakeFiles/chronon3d_sdk_impl.dir/sdk_impl_marker.cpp.o
list(APPEND _sdk_archive_obj_files
    "${CMAKE_BINARY_DIR}/src/CMakeFiles/chronon3d_sdk_impl.dir/sdk_impl_marker.cpp.o")

list(REMOVE_DUPLICATES _sdk_archive_obj_files)
list(SORT _sdk_archive_obj_files)
list(LENGTH _sdk_archive_obj_files _sdk_archive_count)

# ── Stale-manifest guard (canonical semantics; see commit history) ────
#
# ORIGINAL bug: the guard fired at CONFIGURE time whenever the manifest held
# fewer than 2 entries.  That broke the canonical fresh-checkout flow
# (`cmake --preset linux-ci --fresh`) because BEFORE any source is compiled,
# the whole-tree GLOB_RECURSE returns 0 files; only the marker TU is present,
# so count=1 looked indistinguishable from a torn-archive state.
#
# CORRECT semantics: the DoD "marker + >= 1 subsystem" check should ONLY
# fail HARD when at least one .cpp.o IS on disk but the registry-driven
# filter dropped the manifest below threshold.  That is the "torn" case
# (e.g. a source was renamed but the old .cpp.o still lingers).
#
# On a FRESH configure (GLOB=0), the marker-only manifest is the legitimate
# pre-build state and `sdk_archive_merge` populates it via the dependency
# chain `DEPENDS chronon3d_sdk_impl ${CHRONON3D_REGISTRY_OBJECT_LIBS}`.
list(LENGTH _all_cpp_o _all_cpp_o_count)
if(_all_cpp_o_count GREATER 0 AND _sdk_archive_count LESS 2)
    message(FATAL_ERROR
        "sdk_archive_merge: stale manifest.  Found ${_all_cpp_o_count} raw .cpp.o file(s) "
        "on disk but the registry-driven filter retained only ${_sdk_archive_count} "
        "(marker TU only).  This typically indicates a renamed/removed source whose "
        "old .cpp.o lingered in CMakeFiles/<target>.dir/.  Clean the build dir "
        "(`rm -rf build/<dir>`) and re-run the build.")
endif()

if(_sdk_archive_count LESS 2)
    message(STATUS
        "sdk_archive_merge: ${_sdk_archive_count} .o object(s) derived from registry "
        "(fresh build; _all_cpp_o=${_all_cpp_o_count}; populate via "
        "`cmake --build <build-dir> --target sdk_archive_merge`)")
else()
    message(STATUS "sdk_archive_merge: ${_sdk_archive_count} .o objects derived from registry (manifest)")
endif()

# ── POST_BUILD custom target: rebuild the archive from scratch ────────
# `ar crs` (the c = create, r = replace, s = write symbol table) only
# inserts/replaces the entries listed in OBJECT_FILES; entries from a
# prior run that are absent from the new manifest would survive as
# ghosts.  CMake 3.25 does not natively aggregate OBJECT .o into STATIC
# archives, so this custom target is the canonical workaround.
#
# Invoked via a response file (`cmake/sdk_archive_merge.cmake`) so the
# object list is not subject to the shell ARG_MAX limit on systems with
# low per-execve byte caps.
add_custom_target(sdk_archive_merge
    COMMAND ${CMAKE_COMMAND} -E echo "Merging subsystem .o files into SDK archive..."
    COMMAND ${CMAKE_COMMAND} -DARCHIVE="$<TARGET_FILE:chronon3d_sdk_impl>" -DOBJECT_FILES="${_sdk_archive_obj_files}" -DAR="${CMAKE_AR}" -P "${CMAKE_SOURCE_DIR}/cmake/sdk_archive_merge.cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "=== Archive object count after merge ==="
    COMMAND ${CMAKE_AR} t "$<TARGET_FILE:chronon3d_sdk_impl>"
    COMMENT "Merging all subsystem OBJECT .o files into libchronon3d_sdk_impl.a (registry-driven manifest)"
    DEPENDS chronon3d_sdk_impl ${CHRONON3D_REGISTRY_OBJECT_LIBS}
)

# ── Wire `cmake --install` to invoke the merge before installing ──────
# Without this hook, a per-consumer `find_package(Chronon3D)` install
# would expose a marker-only stub archive (CMake 3.25 default for
# STATIC libraries whose only .o is a marker).  The CODE form runs at
# install time against the captured ${CMAKE_BINARY_DIR}, which is
# still valid because install() runs in the binary dir context.
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} --build \${CMAKE_BINARY_DIR} --target sdk_archive_merge)")
