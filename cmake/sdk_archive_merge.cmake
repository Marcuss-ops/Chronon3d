# cmake/sdk_archive_merge.cmake
# POST_BUILD helper: merges all subsystem OBJECT .o files + the marker into
# the SDK STATIC archive.  CMake 3.25 does not aggregate OBJECT .o into
# STATIC archives natively, so this script replaces the archive entirely
# via `ar crs` (create fresh with symbol table).
#
# Parameters (passed via -D from calling CMakeLists.txt):
#   ARCHIVE   — path to libchronon3d_sdk_impl.a
#   BUILD_DIR — CMAKE_BINARY_DIR
#   AR        — path to ar (CMAKE_AR)

cmake_minimum_required(VERSION 3.25)

# Collect all .o files from subsystem build directories (including marker)
file(GLOB_RECURSE _obj_files
    "${BUILD_DIR}/src/*.cpp.o"
    "${BUILD_DIR}/content/*.cpp.o"
)

list(LENGTH _obj_files _count)

# Create archive from scratch with all .o files
if(_obj_files)
    execute_process(
        COMMAND "${AR}" crs "${ARCHIVE}" ${_obj_files}
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ar crs failed with exit code ${_rc}")
    endif()
endif()

message(STATUS "sdk_archive_merge: archive created with ${_count} .o files → ${ARCHIVE}")
