# ==============================================================================
# cmake/bolt_postprocess.cmake — BOLT post-link optimization custom_target
#
# PURPOSE
#   Wires the `bolt-postprocess` custom_target for the F6.3
#   `release-pgo-thinlto-bolt` CMake preset.  BOLT (Binary Optimization and
#   Layout Tool, https://llvm.org/docs/Bolt.html) is an LLVM post-link
#   optimizer that re-layouts the produced ELF binary using profile data
#   collected via `perf record` on the PGO-optimized binary.
#
# USAGE
#   From the top-level CMakeLists.txt (placed alongside the other
#   custom_target blocks, AFTER add_subdirectory() so the chronon3d_cli
#   target is visible):
#     if(CHRONON3D_BOLT_POSTPROCESS)
#         include(cmake/bolt_postprocess.cmake)
#     endif()
#
#   Then invoke via:
#     cmake --preset release-pgo-thinlto-bolt
#     cmake --build build/chronon/release-pgo-thinlto-bolt --target bolt-postprocess
#
# HOW IT WORKS
#   1. Reads `CHRONON3D_BOLT_DATA_PATH` (set by the preset to
#      `${binaryDir}/perf.fdata`) to find the BOLT profile data file.
#   2. Iterates over the produced binaries (chronon3d_cli + chronon3d_sdk_impl)
#      and runs `llvm-bolt <binary> -data <profdata> -o <binary>.bolt -relocs`.
#   3. Reports the `bolted_binary_path` per artifact in the build log.
#
# CONSTRAINTS
#   - Requires `llvm-bolt` on PATH (LLVM toolchain; provided by `clang`+`lld`
#     package).  Forward-point: `tools/check_bolt_available.sh` gate.
#   - The BOLT profile data is DIFFERENT from PGO profile data: BOLT uses
#     `perf record`-sampled `.fdata` (via `perf2bolt`), NOT LLVM's
#     `llvm-profdata`-merged `.profdata`.  Both profiles are required for
#     the combined PGO+ThinLTO+BOLT pipeline; the preset expects them at
#     the documented paths (override via `-DCHRONON3D_PROFILE_DATA_PATH=...`
#     + `-DCHRONON3D_BOLT_DATA_PATH=...`).
#   - NO `-ffast-math` precondition enforced here — the preset sets the
#     base flags without fast-math (per user spec verbatim).
# ==============================================================================

find_program(LLVM_BOLT_EXECUTABLE
    NAMES llvm-bolt bolt
    DOC "BOLT binary optimizer (LLVM)"
)

if(NOT LLVM_BOLT_EXECUTABLE)
    message(FATAL_ERROR
        "F6.3 release-pgo-thinlto-bolt preset requires `llvm-bolt` on PATH. "
        "Install via `apt install llvm` (≥14) or build from LLVM sources. "
        "If you don't need BOLT, use `release-pgo-thinlto` instead (no BOLT step)."
    )
endif()

set(CHRONON3D_BOLT_TARGETS
    chronon3d_cli
    chronon3d_sdk_impl
    CACHE INTERNAL "F6.3 — binaries to bolt-postprocess; default chronon3d_cli + chronon3d_sdk_impl"
)

foreach(bolt_target IN LISTS CHRONON3D_BOLT_TARGETS)
    add_custom_target(bolt-postprocess-${bolt_target}
        COMMAND ${LLVM_BOLT_EXECUTABLE}
                $<TARGET_FILE:${bolt_target}>
                -data ${CHRONON3D_BOLT_DATA_PATH}
                -o $<TARGET_FILE:${bolt_target}>.bolt
                -relocs
        DEPENDS ${bolt_target}
        COMMENT "F6.3 BOLT post-link optimization for ${bolt_target} (profile: ${CHRONON3D_BOLT_DATA_PATH})"
        VERBATIM
    )
endforeach()

# Aggregate target: `cmake --build --target bolt-postprocess` runs all
# per-target BOLT invocations sequentially (single-binary optimization
# is fast enough; parallel risk is the filesystem write pattern).
add_custom_target(bolt-postprocess
    DEPENDS ${CHRONON3D_BOLT_TARGETS}
)
foreach(bolt_target IN LISTS CHRONON3D_BOLT_TARGETS)
    add_dependencies(bolt-postprocess bolt-postprocess-${bolt_target})
endforeach()

message(STATUS "F6.3 BOLT postprocess wired: ${CHRONON3D_BOLT_DATA_PATH} (presets: release-pgo-thinlto-bolt)")
