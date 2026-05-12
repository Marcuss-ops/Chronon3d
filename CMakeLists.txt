cmake_minimum_required(VERSION 3.24)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" ON)
option(CHRONON3D_BUILD_EXAMPLES "Build Chronon3d examples" ON)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)

# Find Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(concurrentqueue CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)

# Core Library
add_library(chronon3d
    src/core/version.cpp # Placeholder if needed
)

# Public headers directory
target_include_directories(chronon3d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(chronon3d
    PUBLIC
        spdlog::spdlog
        fmt::fmt
        nlohmann_json::nlohmann_json
        Tracy::TracyClient
        mimalloc
        Taskflow::Taskflow
        concurrentqueue::concurrentqueue
        absl::flat_hash_map
        absl::status
        absl::statusor
        absl::strings
        absl::flags
        absl::flags_parse
        glm::glm
        simdjson::simdjson
)

target_compile_features(chronon3d PUBLIC cxx_std_20)

# Subdirectories
if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

if(CHRONON3D_BUILD_EXAMPLES)
    # add_subdirectory(examples) # Empty for now
endif()

if(CHRONON3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
