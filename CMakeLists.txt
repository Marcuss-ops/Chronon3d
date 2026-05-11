cmake_minimum_required(VERSION 3.22)
project(Graphis3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Filament SDK Location (Windows variants)
set(FILAMENT_DIR "${CMAKE_SOURCE_DIR}/filament-sdk")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FILAMENT_LIB_DIR "${FILAMENT_DIR}/lib/x86_64/mdd")
    set(FILAMENT_EXTRA_LIBS "${FILAMENT_LIB_DIR}/matdbg.lib")
else()
    set(FILAMENT_LIB_DIR "${FILAMENT_DIR}/lib/x86_64/md")
    set(FILAMENT_EXTRA_LIBS "")
endif()

# Manually define Filament libraries
set(FILAMENT_LIBRARIES
    ${FILAMENT_EXTRA_LIBS}
    "${FILAMENT_LIB_DIR}/filament.lib"
    "${FILAMENT_LIB_DIR}/backend.lib"
    "${FILAMENT_LIB_DIR}/bluegl.lib"
    "${FILAMENT_LIB_DIR}/bluevk.lib"
    "${FILAMENT_LIB_DIR}/filabridge.lib"
    "${FILAMENT_LIB_DIR}/filaflat.lib"
    "${FILAMENT_LIB_DIR}/utils.lib"
    "${FILAMENT_LIB_DIR}/geometry.lib"
    "${FILAMENT_LIB_DIR}/smol-v.lib"
    "${FILAMENT_LIB_DIR}/ibl.lib"
    "${FILAMENT_LIB_DIR}/abseil.lib"
    "${FILAMENT_LIB_DIR}/gltfio.lib"
    "${FILAMENT_LIB_DIR}/gltfio_core.lib"
    "${FILAMENT_LIB_DIR}/meshoptimizer.lib"
    "${FILAMENT_LIB_DIR}/stb.lib"
    "${FILAMENT_LIB_DIR}/zstd.lib"
    "${FILAMENT_LIB_DIR}/basis_transcoder.lib"
    "${FILAMENT_LIB_DIR}/dracodec.lib"
    opengl32.lib
    gdi32.lib
    user32.lib
    shell32.lib
    shlwapi.lib
)

# Find vcpkg dependencies
find_package(SDL2 CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Include Directories
include_directories(include)
include_directories("${FILAMENT_DIR}/include")

# MSVC-specific warning flags and strict standards
add_compile_options(/W4 /permissive- /utf-8)
add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)

# Strict coding standards (clang-tidy if available)
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=cppcoreguidelines-*,modernize-*,bugprone-*;--warnings-as-errors=*")
endif()

# Main Executable
add_executable(Graphis3d
    src/main.cpp
    include/core/FilamentContext.hpp
    include/core/ResourceWrappers.hpp
)

# Link Libraries
target_link_libraries(Graphis3d PRIVATE
    ${FILAMENT_LIBRARIES}
    SDL2::SDL2
    EnTT::EnTT
    spdlog::spdlog
    fmt::fmt
    nlohmann_json::nlohmann_json
)

# Post-build: Copy Filament binaries if needed (mostly for DLLs on Windows)
# (Filament is usually static, but tools like matc might be needed)
