cmake_minimum_required(VERSION 3.24)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    message(WARNING "vcpkg toolchain not detected. Dependencies are strict and might fail to configure. Please use a CMake preset (e.g., --preset linux-release) or set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake.")
endif()

# --- Opzioni di Compilazione Globali ---
if(MSVC)
    add_compile_options(/utf-8 /FS /EHsc /fp:strict)
else()
    add_compile_options(-ffp-contract=off)
endif()

# --- Configurazione Strumenti di Build (ccache / linkers) ---
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER "$ENV{CMAKE_C_COMPILER_LAUNCHER}")
endif()

if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C++ compiler launcher")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C compiler launcher")
    endif()
endif()

if(NOT MSVC AND NOT CMAKE_CXX_LINKER_LAUNCHER)
    find_program(MOLD_PROGRAM mold)
    if(MOLD_PROGRAM)
        message(STATUS "mold found: ${MOLD_PROGRAM}")
        add_link_options("-fuse-ld=mold")
    else()
        find_program(LLD_PROGRAM ld.lld)
        if(LLD_PROGRAM)
            message(STATUS "lld found: ${LLD_PROGRAM}")
            add_link_options("-fuse-ld=lld")
        endif()
    endif()
endif()

# --- Opzioni del Progetto ---
option(CHRONON3D_UNITY_BUILD "Enable unity (jumbo) build" ON)
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" ON)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_BUILD_TOOLS "Build development tools" OFF)
option(CHRONON3D_BUILD_BENCHMARKS "Build Chronon3d benchmarks" OFF)
option(CHRONON3D_BUILD_CONTENT "Build bundled content compositions (proofs, demos)" ON)
option(CHRONON3D_ENABLE_NATIVE_FFMPEG "Enable native FFmpeg/libav encoder support" OFF)
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling" OFF)
option(CHRONON3D_ENABLE_SQLITE_TELEMETRY "Enable SQLite telemetry store" OFF)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    add_compile_definitions(CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()

if(CHRONON3D_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    if(NOT CMAKE_UNITY_BUILD_BATCH_SIZE)
        set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
    endif()
endif()

# --- Ricerca Dipendenze ---
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(blend2d CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(libyuv CONFIG REQUIRED)

if(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()

if(CHRONON3D_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_PROFILING)
    find_package(Tracy CONFIG QUIET)
endif()

# --- Git Commit Hash Extraction ---
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CHRONON3D_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CHRONON3D_GIT_COMMIT)
    set(CHRONON3D_GIT_COMMIT "unknown")
endif()

# ==============================================================================
# SUBDIRECTORIES
# ==============================================================================
add_subdirectory(src)

# 10. Content Generation (optional — proofs/demos, not core engine)
if(CHRONON3D_BUILD_CONTENT)
    add_subdirectory(content)
endif()

# --- Profiling & Condizionali di Build ---
if(CHRONON3D_ENABLE_PROFILING AND Tracy_FOUND)
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

if(CHRONON3D_BUILD_TESTS)
    enable_testing()
    target_compile_definitions(chronon3d_core_impl PUBLIC CHRONON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

if(CHRONON3D_BUILD_TOOLS)
    # preview_new_features target removed - tools/preview_new_features.cpp does not exist
endif()

# ==============================================================================
# C API SHARED LIBRARY & C TEST (Defined at the end to ensure all targets exist)
# ==============================================================================
add_library(chronon3d_shared SHARED
    src/c_api/chronon3d_c_api.cpp
    src/c_api/c_api_context.cpp
    src/c_api/c_api_compile.cpp
    src/c_api/c_api_render.cpp
)
target_link_libraries(chronon3d_shared PRIVATE chronon3d_pipeline)

if(CHRONON3D_BUILD_CONTENT)
    if(TARGET chronon3d_content_registry)
        # --whole-archive ensures all static-registration symbols from content
        # modules (minimalist, anims, text, …) are included in the shared library.
        # Without it the linker drops unreferenced objects whose constructors or
        # registration calls are needed at runtime.
        #
        # We list every content module explicitly inside the whole-archive region
        # because CMake places transitive PUBLIC deps AFTER --no-whole-archive.
        if(NOT MSVC)
            target_link_libraries(chronon3d_shared PRIVATE
                "-Wl,--whole-archive"
                chronon3d_content_registry
                chronon3d_content_anims
                chronon3d_content_minimalist
                chronon3d_content_shapes
                chronon3d_content_images
                chronon3d_content_effects
                chronon3d_content_2d5
                chronon3d_content_grid
                "-Wl,--no-whole-archive"
            )
        else()
            target_link_libraries(chronon3d_shared PRIVATE chronon3d_content_registry)
        endif()
    endif()
    target_compile_definitions(chronon3d_shared PRIVATE CHRONON3D_BUILD_CONTENT)
endif()

target_include_directories(chronon3d_shared PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(chronon3d_shared PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_shared PROPERTIES
    OUTPUT_NAME "chronon3d"
    POSITION_INDEPENDENT_CODE ON
    UNITY_BUILD OFF
)
target_compile_definitions(chronon3d_shared PRIVATE CHRONON3D_BUILD_SHARED)

if(CHRONON3D_BUILD_TESTS)
    if(CHRONON3D_BUILD_CONTENT)
        add_executable(test_c_api tests/test_c_api.c)
        target_link_libraries(test_c_api PRIVATE chronon3d_shared)
        target_include_directories(test_c_api PRIVATE ${CMAKE_SOURCE_DIR}/include)
        add_test(NAME test_c_api COMMAND test_c_api WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()

    # Minimal C API test — works with or without content modules
    add_executable(test_c_api_minimal tests/test_c_api_minimal.c)
    target_link_libraries(test_c_api_minimal PRIVATE chronon3d_shared)
    target_include_directories(test_c_api_minimal PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME test_c_api_minimal COMMAND test_c_api_minimal WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

