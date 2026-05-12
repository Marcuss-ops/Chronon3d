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
find_package(Tracy CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)

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
)
target_compile_features(chronon3d INTERFACE cxx_std_20)

# 2. Renderer Library (Static)
add_library(chronon3d_renderer STATIC
    src/renderer/software_renderer.cpp
    src/renderer/text_renderer.cpp
)
target_link_libraries(chronon3d_renderer
    PUBLIC
        chronon3d
    PRIVATE
        hwy::hwy
)
if(CHRONON3D_ENABLE_PROFILING)
    target_link_libraries(chronon3d_renderer PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_renderer PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

# 3. Pipeline Library (Heavy)
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline
    INTERFACE
        chronon3d_renderer
        Taskflow::Taskflow
        concurrentqueue::concurrentqueue
)
if(CHRONON3D_ENABLE_PROFILING)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

# 4. IO Library
add_library(chronon3d_io STATIC
    src/io/image_writer.cpp
)
target_link_libraries(chronon3d_io
    PUBLIC
        chronon3d
)
target_include_directories(chronon3d_io PRIVATE ${Stb_INCLUDE_DIR})

# 5. Examples Library (Host for auto-registration)
add_library(chronon3d_examples_lib STATIC
    examples/foundation_proof.cpp
    examples/basic_shapes.cpp
    examples/moving_rect.cpp
    examples/animated_title_card.cpp
    examples/visual_quality_proof.cpp
    examples/axis_debug.cpp
    examples/blending_showcase.cpp
    examples/code_first_smoke.cpp
    examples/remotion_style_guide.cpp
    examples/text_proof.cpp
)
target_link_libraries(chronon3d_examples_lib
    PUBLIC
        chronon3d
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
