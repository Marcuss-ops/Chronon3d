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

# Find Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)

# Core Library (Minimal)
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d
    INTERFACE
        spdlog::spdlog
        fmt::fmt
        glm::glm
        absl::flat_hash_map
)

# Pipeline Library (Heavy)
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline
    INTERFACE
        chronon3d
        Tracy::TracyClient
        Taskflow::Taskflow
        concurrentqueue::concurrentqueue
)

# IO Library
add_library(chronon3d_io STATIC
    src/io/image_writer.cpp
)
target_link_libraries(chronon3d_io
    PUBLIC
        chronon3d
)
target_include_directories(chronon3d_io PRIVATE ${Stb_INCLUDE_DIR})

# Compile features
target_compile_features(chronon3d INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(chronon3d INTERFACE /EHsc)
    target_compile_definitions(chronon3d INTERFACE _DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION)
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
