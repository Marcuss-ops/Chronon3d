cmake_minimum_required(VERSION 3.24)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Options
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" ON)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_BUILD_EXAMPLES "Build Chronon3d examples" ON)
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling (creates background thread, not for release)" OFF)

# Find Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(Tracy CONFIG QUIET)

if(CHRONON3D_ENABLE_PROFILING AND NOT Tracy_FOUND)
    message(FATAL_ERROR "CHRONON3D_ENABLE_PROFILING is ON, but Tracy was not found")
endif()

# 1. Base Core Library (Minimal)
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d
    INTERFACE
        glm::glm
        meshoptimizer::meshoptimizer
        xxHash::xxhash
)
target_compile_features(chronon3d INTERFACE cxx_std_20)

# 2. Registry Library (Static)
file(GLOB CHRONON3D_REGISTRY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/registry/*.cpp"
)

add_library(chronon3d_registry STATIC
    ${CHRONON3D_REGISTRY_SOURCES}
)
target_link_libraries(chronon3d_registry
    PUBLIC
        chronon3d
)

# 3. Cache Library (Static)
file(GLOB CHRONON3D_CACHE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cache/*.cpp"
)

add_library(chronon3d_cache STATIC
    ${CHRONON3D_CACHE_SOURCES}
)
target_link_libraries(chronon3d_cache
    PUBLIC
        chronon3d
)

# 4. Render Graph & Video sources (now part of renderer)
file(GLOB CHRONON3D_RENDER_GRAPH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render_graph/*.cpp"
)
file(GLOB CHRONON3D_VIDEO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/video/*.cpp"
)

# 5. Effects Library (Static)
file(GLOB CHRONON3D_EFFECTS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/effects/*.cpp"
)

add_library(chronon3d_effects STATIC
    ${CHRONON3D_EFFECTS_SOURCES}
)
target_link_libraries(chronon3d_effects
    PUBLIC
        chronon3d
)

# 6. Renderer Library (Static)
file(GLOB CHRONON3D_RENDERER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/evaluation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/layout/*.cpp"
)

add_library(chronon3d_renderer STATIC
    ${CHRONON3D_RENDERER_SOURCES}
    ${CHRONON3D_RENDER_GRAPH_SOURCES}
    ${CHRONON3D_VIDEO_SOURCES}
)
target_link_libraries(chronon3d_renderer
    PUBLIC
        chronon3d
        chronon3d_registry
        chronon3d_cache
        chronon3d_effects
        chronon3d_io
    PRIVATE
        spdlog::spdlog
        fmt::fmt
        hwy::hwy
)
if(CHRONON3D_ENABLE_PROFILING AND Tracy_FOUND)
    target_link_libraries(chronon3d_renderer PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_renderer PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()
target_include_directories(chronon3d_renderer PRIVATE ${Stb_INCLUDE_DIR})

# 7. Pipeline Library (Heavy)
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline
    INTERFACE
        chronon3d_renderer
        chronon3d_registry
        chronon3d_cache
        Taskflow::Taskflow
        concurrentqueue::concurrentqueue
)
if(CHRONON3D_ENABLE_PROFILING)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

# 8. IO Library
file(GLOB CHRONON3D_IO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/*.cpp"
)

add_library(chronon3d_io STATIC
    ${CHRONON3D_IO_SOURCES}
)
target_link_libraries(chronon3d_io
    PUBLIC
        chronon3d
)
target_include_directories(chronon3d_io PRIVATE ${Stb_INCLUDE_DIR})

# 9. Examples Library (Host for auto-registration)
add_library(chronon3d_examples_lib STATIC)
file(GLOB CHRONON3D_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/basics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/demos/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/proofs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/proofs/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/proofs/fake3d/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/proofs/camera/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/debug/*.cpp"
)
target_sources(chronon3d_examples_lib PRIVATE ${CHRONON3D_EXAMPLE_SOURCES})
target_link_libraries(chronon3d_examples_lib
    PUBLIC
        chronon3d
        chronon3d_renderer
)

# Global compiler options
if(MSVC)
    target_compile_options(chronon3d INTERFACE /EHsc /fp:strict /utf-8)
    target_compile_definitions(chronon3d INTERFACE _DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION)
else()
    target_compile_options(chronon3d INTERFACE -ffp-contract=off)
endif()

# Subdirectories
if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

if(CHRONON3D_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CHRONON3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
