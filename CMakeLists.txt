cmake_minimum_required(VERSION 3.25)

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
option(CHRONON3D_UNITY_BUILD "Enable unity (jumbo) build" OFF)
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" OFF)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_BUILD_C_API "Build experimental C bindings (shared library + C tests)" OFF)
option(CHRONON3D_BUILD_BENCHMARKS "Build Chronon3d benchmarks" OFF)
option(CHRONON3D_BUILD_CONTENT "Build bundled content compositions (proofs, demos)" ON)
option(CHRONON3D_ENABLE_NATIVE_FFMPEG "Enable native FFmpeg/libav encoder support" OFF)
option(CHRONON3D_ENABLE_TEXT "Enable text rendering support (freetype + harfbuzz)" ON)
option(CHRONON3D_ENABLE_EXR "Enable OpenEXR image format support" ON)
option(CHRONON3D_ENABLE_LIBYUV "Enable libyuv SIMD-accelerated YUV conversion" ON)
option(CHRONON3D_ENABLE_VIDEO "Enable video/media pipeline (frame conversion, video sinks)" ON)
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling" OFF)
option(CHRONON3D_ENABLE_SQLITE_TELEMETRY "Enable SQLite telemetry store" OFF)
option(CHRONON3D_ENABLE_MESH "Enable mesh processing support" ON)

# ── Optional heavy dependencies ───────────────────────────────────────
# These gate find_package() calls so that a minimal build (all OFF)
# only needs spdlog, fmt, TBB, concurrentqueue, glm, xxHash, magic_enum,
# tomlplusplus, nlohmann_json, Stb, and Highway SIMD.
option(CHRONON3D_USE_BLEND2D "Enable Blend2D software rendering backend" ON)
option(CHRONON3D_USE_FREETYPE "Enable FreeType font loading" ON)
option(CHRONON3D_USE_HARFBUZZ "Enable HarfBuzz text shaping" ON)
option(CHRONON3D_USE_FRIBIDI "Enable FriBidi bidirectional text support" ON)
option(CHRONON3D_USE_OPENEXR "Enable OpenEXR image format support" ON)
option(CHRONON3D_USE_LIBYUV "Enable libyuv video colour conversion" ON)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    add_compile_definitions(CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()

# ── Propagate optional-dependency defines so code can guard #includes ──
if(CHRONON3D_USE_BLEND2D)
    add_compile_definitions(CHRONON3D_USE_BLEND2D)
endif()
if(CHRONON3D_ENABLE_TEXT)
    add_compile_definitions(CHRONON3D_ENABLE_TEXT)
endif()
if(CHRONON3D_ENABLE_EXR)
    add_compile_definitions(CHRONON3D_ENABLE_EXR)
endif()
if(CHRONON3D_ENABLE_LIBYUV)
    add_compile_definitions(CHRONON3D_ENABLE_LIBYUV)
endif()
if(CHRONON3D_ENABLE_VIDEO)
    add_compile_definitions(CHRONON3D_ENABLE_VIDEO)
endif()
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
if(CHRONON3D_ENABLE_MESH)
    find_package(meshoptimizer CONFIG REQUIRED)
endif()
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
if(CHRONON3D_BUILD_CLI)
    find_package(CLI11 CONFIG REQUIRED)
endif()
find_package(hwy CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
if(CHRONON3D_USE_BLEND2D)
    find_package(blend2d CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_TEXT)
    find_package(freetype CONFIG REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

if(CHRONON3D_ENABLE_EXR)
    find_package(OpenEXR CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_LIBYUV)
    find_package(libyuv CONFIG REQUIRED)
endif()
    find_package(libyuv CONFIG REQUIRED)
endif()

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

# ── Aggregate developer target (CLI + fast tests) ──────────────────────────────
# Convenience target for daily development: builds the CLI and the fast test suite
# in a single invocation. Only created when both CLI and tests are enabled.
if(CHRONON3D_BUILD_CLI AND CHRONON3D_BUILD_TESTS)
    add_custom_target(chronon3d_dev_fast
        DEPENDS
            chronon3d_cli
            chronon3d_tests_fast
    )
endif()

# CHRONON3D_BUILD_TOOLS removed — the option was a no-op (the only target
# it guarded, preview_new_features, was deleted in a previous cleanup).

# ==============================================================================
# C API SHARED LIBRARY & C TEST (Experimental — opt-in via CHRONON3D_BUILD_C_API)
# ==============================================================================
# The C bindings expose JSON → TOML → specscene compilation via a C ABI for
# interop with Python, Rust, and other languages. They are NOT part of the
# core engine surface and are opt-in to keep the default build lean.
#
# Files live in bindings/c/ rather than src/c_api/ to make the isolation clear.
if(CHRONON3D_BUILD_C_API)
    add_library(chronon3d_shared SHARED
        bindings/c/chronon3d_c_api.cpp
        bindings/c/c_api_context.cpp
        bindings/c/c_api_compile.cpp
        bindings/c/c_api_render.cpp
    )
    target_link_libraries(chronon3d_shared PRIVATE chronon3d_pipeline)

    if(CHRONON3D_BUILD_CONTENT)
        if(TARGET chronon3d_content_registry)
            # Content modules use explicit ExtensionRegistry registration (not static-init
            # macros), so the linker preserves registration symbols without --whole-archive
            # on Linux. CMake's PUBLIC/INTERFACE propagation ensures all STATIC deps are
            # linked. On MSVC the linker never drops unreferenced symbols from STATIC
            # libraries linked into a SHARED target.
            target_link_libraries(chronon3d_shared PRIVATE
                chronon3d_content_registry
                chronon3d_all_content_modules
            )
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

        # Aggregate target for CI: shared library + both C API tests
        add_custom_target(chronon3d_c_api_tests
            DEPENDS
                chronon3d_shared
                test_c_api_minimal
                $<IF:$<BOOL:${CHRONON3D_BUILD_CONTENT}>,test_c_api,>
        )
    endif()
endif()

