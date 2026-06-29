# cmake/sdk_archive_merge.cmake
# POST_BUILD helper: merges all subsystem OBJECT .o files + the marker into
# the SDK STATIC archive.  CMake 3.25 does not aggregate OBJECT .o into
# STATIC archives natively, so this script replaces the archive entirely
# via `ar crs` (create fresh with symbol table).
#
# Parameters (passed via -D from calling CMakeLists.txt):
#   ARCHIVE      — path to libchronon3d_sdk_impl.a
#   OBJECT_FILES — semicolon-separated list of .o paths, derived at
#                  configure time in src/CMakeLists.txt from
#                  CHRONON3D_REGISTRY_OBJECT_LIBS (no GLOB_* here).
#   AR           — path to ar (CMAKE_AR)

cmake_minimum_required(VERSION 3.25)

# Filter to existing files, sort, dedupe — defensive guard for missing
# .o files under feature-flag flips or unity-build batching.
set(_obj_files "")
foreach(_obj IN LISTS OBJECT_FILES)
    if(EXISTS "${_obj}")
        list(APPEND _obj_files "${_obj}")
    endif()
endforeach()

list(SORT _obj_files)
list(REMOVE_DUPLICATES _obj_files)
list(LENGTH _obj_files _count)

# Force archive rebuild from scratch: `ar crs` (the c = create, r =
# replace, s = write symbol table) only inserts/replaces the files
# listed; entries present in a previous archive that are absent from
# the new manifest survive as ghosts.  REMOVE-ing the archive first
# guarantees the new merge cannot leak residual .o from a prior
# configuration (e.g., diagnostics were ON in a previous build, then
# switched OFF — chronon3d_*_diagnostics.cpp.o must not survive).
if(EXISTS "${ARCHIVE}")
    file(REMOVE "${ARCHIVE}")
endif()

# Build the archive via a RESPONSE file so the object list is not subject
# to the shell ARG_MAX limit.  A command line of 300+ absolute .o paths
# can exceed LOW ARG_MAX systems (e.g. macOS default 256 KB per execve,
# some embedded CI containers); a response file breaks the limit because
# only the path to `@<rsp>` sits on the argv.
#
# Format: `<archive>\n<obj1>\n<obj2>\n…\n`
#   `ar @file` reads the first non-flag token as the archive name and
#   the rest as member files (binutils 2.46 + CMake 3.25+).
#
# MRI fallback (`ar -M`) is documented in the commit message; keep the
# old inline-argv ar invocation here as a comment for future readers.
if(_count GREATER 0)
    # Place the response file alongside ARCHIVE.  In `-P` script mode
    # CMAKE_CURRENT_BINARY_DIR is not auto-populated; ARCHIVE is the only
    # fully-qualified path we have, so derive the rsp filename from its
    # directory via get_filename_component (purely local, no target graph).
    get_filename_component(_archive_dir "${ARCHIVE}" DIRECTORY)
    set(_rsp "${_archive_dir}/sdk_archive.rsp")
    file(WRITE "${_rsp}" "${ARCHIVE}\n")
    foreach(_obj IN LISTS _obj_files)
        file(APPEND "${_rsp}" "${_obj}\n")
    endforeach()

    # Legacy fallback (kept as comment, response-file is canonical):
    #   execute_process(COMMAND "${AR}" crs "${ARCHIVE}" ${_obj_files} ...)
    execute_process(
        COMMAND "${AR}" crs "@${_rsp}"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        # Preserve the response file for post-mortem inspection.
        message(FATAL_ERROR
            "ar crs via response file failed with exit code ${_rc}; "
            "response file preserved at: ${_rsp}")
    endif()
    file(REMOVE "${_rsp}")
endif()

message(STATUS "sdk_archive_merge: invoked via response-file, n=${_count} → ${ARCHIVE}")
