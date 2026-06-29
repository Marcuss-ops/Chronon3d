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

# Create archive from scratch with all .o files (registry-driven manifest).
if(_count GREATER 0)
    execute_process(
        COMMAND "${AR}" crs "${ARCHIVE}" ${_obj_files}
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ar crs failed with exit code ${_rc}")
    endif()
endif()

message(STATUS "sdk_archive_merge: archive created with ${_count} .o files → ${ARCHIVE}")
