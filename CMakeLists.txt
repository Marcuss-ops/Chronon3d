cmake_minimum_required(VERSION 3.27)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES C CXX
)

# Keep compilation parallel while serializing memory-heavy Ninja link steps.
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL PROPERTY JOB_POOLS chronon3d_link_pool=1)
    set(CMAKE_JOB_POOL_LINK chronon3d_link_pool)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    message(WARNING
        "vcpkg toolchain not detected. Dependencies are strict and may fail to configure. "
        "Use a CMake preset (for example --preset linux-fast-dev) or set CMAKE_TOOLCHAIN_FILE."
    )
endif()

# Deterministic floating-point contraction policy, expressed per compiler.
if(MSVC)
    add_compile_options(/fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-ffp-contract=off)
endif()

# Compiler launcher: explicit caller configuration wins, then environment,
# then an auto-discovered ccache.
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
endif()

if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C++ compiler launcher")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C compiler launcher")
    endif()
endif()

# Linker choice has one authority. Presets do not force mold/lld/gold.
if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_CXX_LINKER_LAUNCHER)
    find_program(MOLD_PROGRAM mold)
    if(MOLD_PROGRAM)
        message(STATUS "mold found: ${MOLD_PROGRAM}")
        add_link_options("-fuse-ld=mold")
    else()
        find_program(LLD_PROGRAM ld.lld)
        if(LLD_PROGRAM)
            message(STATUS "lld found: ${LLD_PROGRAM}")
            add_link_options("-fuse-ld=lld")
        else()
            find_program(GOLD_PROGRAM ld.gold)
            if(GOLD_PROGRAM)
                message(STATUS "gold found: ${GOLD_PROGRAM}")
                add_link_options("-fuse-ld=gold")
            endif()
        endif()
    endif()
endif()

# Project options.
option(CHRONON3D_UNITY_BUILD "Enable unity (jumbo) build" OFF)
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" OFF)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_BUILD_BENCHMARKS "Build Chronon3d benchmarks and benchmark CLI" OFF)
option(CHRONON3D_ENABLE_NATIVE_FFMPEG "Enable native FFmpeg/libav encoder support" OFF)
option(CHRONON3D_ENABLE_TEXT "Enable text rendering support (freetype + harfbuzz + ICU boundaries)" ON)
option(CHRONON3D_ENABLE_SVG "Enable SVG path/document importing" OFF)
option(CHRONON3D_ENABLE_OCIO "Enable OpenColorIO colour-management transforms" OFF)
option(CHRONON3D_ENABLE_EXR "Enable OpenEXR image format support" OFF)
option(CHRONON3D_ENABLE_VIDEO "Enable video/media pipeline (frame conversion, video sinks)" ON)
option(CHRONON3D_ENABLE_TRACING "Enable timeline tracing (Perfetto track-event backend)" OFF)
option(CHRONON3D_ENABLE_TELEMETRY "Enable SQLite telemetry persistence" OFF)
option(CHRONON3D_ENABLE_DIAGNOSTICS "Enable engine-level diagnostics code" OFF)
option(CHRONON3D_ENABLE_VERIFICATION "Build canonical output verification and render receipts" OFF)
option(CHRONON3D_ENABLE_MESH "Enable mesh processing support" OFF)
option(CHRONON3D_BUILD_C_API "Build the shared C ABI wrapper" OFF)
option(CHRONON3D_ENABLE_IPC "Build the FlatBuffers IPC boundary" OFF)
option(CHRONON3D_BUILD_DAEMON "Build the persistent IPC daemon" OFF)
option(CHRONON3D_ENABLE_VULKAN "Build the headless Vulkan backend" OFF)
option(CHRONON3D_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(CHRONON3D_ENABLE_CUDA_INTEROP
       "Build CUDA/Vulkan external-memory interop prerequisites" OFF)
option(CHRONON3D_DEV_SPLIT_DWARF
       "Use split DWARF debug information in developer builds" OFF)
option(CHRONON3D_BUILD_TIME_TRACE
       "Emit Clang -ftime-trace JSON files for compile-time profiling" OFF)
option(CHRONON3D_BOLT_POSTPROCESS
       "Enable LLVM BOLT post-link optimization target" OFF)
option(CHRONON3D_REPRODUCIBLE_BUILD
       "Normalize source/debug paths for reproducible artifact comparisons" OFF)
option(CHRONON3D_ENABLE_CRASH_HANDLER
       "Enable cpptrace-based crash handler" OFF)
option(CHRONON3D_ENABLE_DEV_WATCH
       "Enable file-system watch mode for development" OFF)

if(CHRONON3D_ENABLE_SVG)
    find_package(boost_spirit CONFIG REQUIRED)
    find_package(boost_math CONFIG REQUIRED)
    include(FetchContent)
    FetchContent_Declare(
        svgpp
        URL https://github.com/svgpp/svgpp/archive/refs/tags/v1.3.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(svgpp)
    FetchContent_GetProperties(svgpp)
    add_library(chronon3d_svgpp_headers INTERFACE)
    target_include_directories(chronon3d_svgpp_headers INTERFACE
        "${svgpp_SOURCE_DIR}/include"
    )
    target_link_libraries(chronon3d_svgpp_headers INTERFACE Boost::spirit Boost::math)
endif()

if(CHRONON3D_DEV_SPLIT_DWARF AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:Debug>:-gsplit-dwarf>>
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:RelWithDebInfo>:-gsplit-dwarf>>
    )
endif()

if(CHRONON3D_BUILD_TIME_TRACE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ftime-trace>)
endif()

if(CHRONON3D_REPRODUCIBLE_BUILD AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>
        $<$<COMPILE_LANGUAGE:C,CXX>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.>
    )
endif()

# Blend2D is independent from text and remains part of the lean software path.
option(CHRONON3D_USE_BLEND2D "Enable Blend2D software rendering backend" ON)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    add_compile_definitions(CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()
if(CHRONON3D_ENABLE_CUDA_INTEROP)
    add_compile_definitions(CHRONON3D_ENABLE_CUDA_INTEROP)
    include(cmake/ChrononCuda.cmake)
    chronon3d_configure_cuda()
endif()
if(CHRONON3D_USE_BLEND2D)
    add_compile_definitions(CHRONON3D_USE_BLEND2D)
endif()
if(CHRONON3D_ENABLE_TEXT)
    add_compile_definitions(CHRONON3D_ENABLE_TEXT)
endif()
if(CHRONON3D_ENABLE_EXR)
    add_compile_definitions(CHRONON3D_ENABLE_EXR)
endif()
if(CHRONON3D_ENABLE_VIDEO)
    add_compile_definitions(CHRONON3D_ENABLE_VIDEO)
endif()

# Tests certify diagnostics snapshots and verification receipts, so these
# features are canonical consequences of CHRONON3D_BUILD_TESTS.
if(CHRONON3D_BUILD_TESTS)
    set(CHRONON3D_ENABLE_DIAGNOSTICS ON CACHE BOOL "Enable engine-level diagnostics code" FORCE)
    set(CHRONON3D_ENABLE_VERIFICATION ON CACHE BOOL "Build canonical output verification and render receipts" FORCE)
endif()
if(CHRONON3D_ENABLE_DIAGNOSTICS)
    add_compile_definitions(CHRONON3D_ENABLE_DIAGNOSTICS)
endif()

if(CHRONON3D_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    if(NOT CMAKE_UNITY_BUILD_BATCH_SIZE)
        set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
    endif()
endif()

# Dependencies.
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
if(CHRONON3D_ENABLE_MESH)
    find_package(meshoptimizer CONFIG REQUIRED)
endif()
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
if(CHRONON3D_BUILD_CLI)
    find_package(CLI11 CONFIG REQUIRED)
endif()
if(CHRONON3D_ENABLE_IPC)
    find_package(flatbuffers CONFIG REQUIRED)
    find_program(CHRONON3D_FLATC flatc REQUIRED)
endif()
find_package(hwy CONFIG REQUIRED)
if(CHRONON3D_USE_BLEND2D)
    find_package(blend2d CONFIG REQUIRED)
endif()
find_package(zstd CONFIG REQUIRED)

if(CHRONON3D_ENABLE_TEXT)
    find_package(freetype CONFIG REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(ICU REQUIRED COMPONENTS uc i18n)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
if(CHRONON3D_ENABLE_OCIO)
    find_package(OpenColorIO CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    find_package(unofficial-spirv-reflect CONFIG REQUIRED)
    if(TARGET unofficial::spirv-reflect)
        add_library(chronon3d_spirv_reflect_headers INTERFACE)
        target_link_libraries(chronon3d_spirv_reflect_headers INTERFACE
            unofficial::spirv-reflect
        )
        find_path(CHRONON3D_SPIRV_REFLECT_INCLUDE_ROOT
            NAMES spirv_reflect.h
            HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        )
        if(CHRONON3D_SPIRV_REFLECT_INCLUDE_ROOT)
            target_include_directories(chronon3d_spirv_reflect_headers SYSTEM
                INTERFACE "${CHRONON3D_SPIRV_REFLECT_INCLUDE_ROOT}"
            )
        else()
            message(FATAL_ERROR
                "Could not find spirv_reflect.h include root for unofficial::spirv-reflect")
        endif()
    endif()
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_EXR)
    find_package(OpenEXR CONFIG REQUIRED)
endif()
if(CHRONON3D_ENABLE_TELEMETRY)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()
if(CHRONON3D_ENABLE_CRASH_HANDLER)
    find_package(cpptrace CONFIG REQUIRED)
endif()
if(CHRONON3D_ENABLE_DEV_WATCH)
    find_package(efsw CONFIG REQUIRED)
endif()
if(CHRONON3D_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()
if(CHRONON3D_ENABLE_TRACING)
    find_package(unofficial-perfetto CONFIG REQUIRED)
endif()

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

# Central build registries and source graph.
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DRegistry.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/ChrononShaders.cmake)

if(CHRONON3D_ENABLE_VULKAN)
    add_subdirectory(tools/shaders)
endif()

add_subdirectory(src)

if(CHRONON3D_ENABLE_TRACING)
    target_link_libraries(chronon3d_core_impl PRIVATE unofficial::perfetto::perfetto)
    target_compile_definitions(chronon3d_core_impl PRIVATE CHRONON3D_ENABLE_TRACING)
    target_link_libraries(chronon3d_pipeline INTERFACE unofficial::perfetto::perfetto)
endif()

# Applications are materialized before tests so test manifests may reference
# their registered targets.
if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()
if(CHRONON3D_BUILD_DAEMON)
    if(NOT CHRONON3D_ENABLE_IPC)
        message(FATAL_ERROR "CHRONON3D_BUILD_DAEMON requires CHRONON3D_ENABLE_IPC")
    endif()
    add_subdirectory(apps/chronon3d_daemon)
endif()
if(CHRONON3D_BUILD_BENCHMARKS)
    add_subdirectory(apps/chronon3d_bench)
endif()

# BOLT is a post-link executable optimization and must be wired only after the
# CLI target exists.
if(CHRONON3D_BOLT_POSTPROCESS)
    include(${CMAKE_SOURCE_DIR}/cmake/bolt_postprocess.cmake)
endif()

# Tests and fuzzers.
enable_testing()
if(CHRONON3D_BUILD_TESTS)
    set(BUILD_TESTING ON CACHE BOOL "Build the testing tree.")
    target_compile_definitions(chronon3d_core_impl PUBLIC CHRONON_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CHRONON3D_BUILD_FUZZERS AND CHRONON3D_ENABLE_IPC)
    add_subdirectory(tests/fuzz)
endif()

# Install-boundary CI gate.
option(CHRONON3D_BUILD_INSTALL_CONSUMER_TEST
    "Register the end-to-end SDK install-boundary check" OFF)
if(CHRONON3D_BUILD_INSTALL_CONSUMER_TEST AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(
        NAME install_consumer_ci
        COMMAND "${CMAKE_SOURCE_DIR}/tools/install_consumer_test.sh"
    )
    set_tests_properties(install_consumer_ci PROPERTIES
        TIMEOUT 900
        LABELS "boundary;ci"
        ENVIRONMENT "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE};CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH};SDK_BUILD=${CMAKE_BINARY_DIR};SDK_PREFIX=${CMAKE_BINARY_DIR}/install-consumer-prefix;CHRONON3D_INSTALL_TEST_PRESET=linux-ci-full-validation;CHRONON3D_INSTALL_TEST_FAST=1"
    )
endif()

# Visual regression gate.
option(CHRONON3D_BUILD_VISUAL_REGRESSION_GATE
    "Register the two-tier visual regression CTest gate" ON)
if(CHRONON3D_BUILD_VISUAL_REGRESSION_GATE)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(
            NAME chronon3d_visual_regression_gate
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/tools/visual_regression_gate.py"
                --actual "$ENV{VISUAL_GATE_ACTUAL}"
                --reference "$ENV{VISUAL_GATE_REFERENCE}"
                --output-dir "${CMAKE_BINARY_DIR}/test_renders/visual_gate"
        )
        set_tests_properties(chronon3d_visual_regression_gate PROPERTIES
            TIMEOUT 60
            LABELS "gate;visual;ci"
            SKIP_RETURN_CODE 2
        )
    endif()
endif()

# SDK install/export boundary.
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DSdkInstall.cmake)

if(CHRONON3D_BUILD_CLI AND CHRONON3D_BUILD_TESTS)
    add_custom_target(chronon3d_dev_fast
        DEPENDS chronon3d_cli chronon3d_tests_fast
    )
endif()

include(cmake/Hygiene.cmake)

# Architecture aggregate target.
add_custom_target(chronon3d_architecture_check)
foreach(_architecture_dependency
        chronon3d_runtime
        chronon3d_graph_executor
        chronon3d_graph_pipeline
        chronon3d_backend_software
        chronon3d_sdk_impl)
    if(TARGET ${_architecture_dependency})
        add_dependencies(chronon3d_architecture_check ${_architecture_dependency})
    endif()
endforeach()
