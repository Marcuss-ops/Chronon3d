# ==============================================================================
# cmake/sdk_archive_merge.cmake — POST_BUILD merge script
#
# PURPOSE
#   Invoked by the `sdk_archive_merge` custom target in
#   `cmake/Chronon3DSdkArchive.cmake` once every per-subsystem .o file is
#   on disk.  Runs `ar crs` to rebuild `libchronon3d_sdk_impl.a` from the
#   registry-driven manifest, replacing the marker-only stub that CMake
#   3.25 emits by default for STATIC libraries whose only `.o` is the
#   marker TU.  Documented in TICKET-011 cmake-boundary (single
#   aggregated archive contract).
#
# INPUTS   (all set via -D by the caller)
#   ARCHIVE       Absolute path of the .a file (target-file expansion of
#                 `chronon3d_sdk_impl`).
#   OBJECT_FILES  ;-separated CMake list of `.cpp.o` paths emitted by the
#                 manifest filter (registrty-driven GLOB_RECURSE +
#                 `/<target>.dir/` substring filter + marker TU).
#   AR            Absolute path to the LLVM/binutils `ar` tool
#                 (`${CMAKE_AR}` from the caller's configure context).
#
# OUTPUT  rc=0 on archive merge + (rc=0) `ar` exit; rc=FATAL otherwise.
# ==============================================================================

if(NOT DEFINED ARCHIVE OR NOT DEFINED OBJECT_FILES OR NOT DEFINED AR)
    message(FATAL_ERROR
        "sdk_archive_merge.cmake: missing -D'd input. "
        "Required: ARCHIVE, OBJECT_FILES, AR.  See the `sdk_archive_merge` "
        "custom_target in cmake/Chronon3DSdkArchive.cmake for the caller.")
endif()

# Route through CMake's NATIVE_COMMAND splitter so the resulting argv
# passes the per-execve byte-cap (ARG_MAX) check on platforms with low
# caps (sandbox / docker with custom RLIMIT_AS / seccomp profiles).
# On Linux glibc ARG_MAX is ample, but the protection is essentially
# free and matches the documented "response-file" rationale in the
# surrounding header comment.
separate_arguments(_merged_objects NATIVE_COMMAND "${OBJECT_FILES}")

# `ar crs` = create + replace + write symbol table.  Each `OBJECT_FILES`
# entry is inserted/replaced in the archive; entries from a prior run
# absent from the new manifest survive as ghosts (acceptable — the
# caller wipes `${ARCHIVE}` between merges via `cmake --fresh` on
# boundary CI).
execute_process(
    COMMAND ${AR} crs "${ARCHIVE}" ${_merged_objects}
    RESULT_VARIABLE _ar_rc
)
if(NOT _ar_rc EQUAL 0)
    list(LENGTH _merged_objects _merged_objects_count)
    message(FATAL_ERROR
        "sdk_archive_merge: ar crs \"${ARCHIVE}\" failed "
        "(rc=${_ar_rc}, manifest size=${_merged_objects_count} .o entries).  "
        "Inspect /tmp/<build>/src/CMakeFiles/*.dir/ for missing/renamed TUs.")
endif()

# Informational: confirm the archive is non-empty post-merge so the
# cmake-time stale-manifest guard can rely on raw .o count alone.
execute_process(
    COMMAND ${AR} t "${ARCHIVE}"
    RESULT_VARIABLE _ar_t_rc
    OUTPUT_VARIABLE _ar_t_out
)
if(_ar_t_rc EQUAL 0)
    message(STATUS "sdk_archive_merge: archive \"${ARCHIVE}\" populated")
else()
    # `ar t` is best-effort; do not fail the build on a non-fatal read.
    message(WARNING "sdk_archive_merge: ar t verification returned rc=${_ar_t_rc}")
endif()
