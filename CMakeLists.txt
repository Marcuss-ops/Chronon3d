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
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling" OFF)
option(CHRONON3D_ENABLE_VIDEO "Enable FFmpeg-based video output and tests" OFF)

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

# FFmpeg Setup
if(CHRONON3D_ENABLE_VIDEO)
    if(NOT DEFINED VCPKG_INSTALLED_DIR)
        set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed")
    endif()
    if(NOT DEFINED VCPKG_TARGET_TRIPLET)
        if(WIN32)
            set(VCPKG_TARGET_TRIPLET "x64-windows")
        elseif(UNIX)
            set(VCPKG_TARGET_TRIPLET "x64-linux")
        endif()
    endif()

    set(_chronon3d_ffmpeg_include_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    set(_chronon3d_ffmpeg_lib_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")

    find_path(FFMPEG_AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS "${_chronon3d_ffmpeg_include_dir}" NO_DEFAULT_PATH)
    find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec PATHS "${_chronon3d_ffmpeg_lib_dir}" NO_DEFAULT_PATH)
    find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat PATHS "${_chronon3d_ffmpeg_lib_dir}" NO_DEFAULT_PATH)
    find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil PATHS "${_chronon3d_ffmpeg_lib_dir}" NO_DEFAULT_PATH)
    find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale PATHS "${_chronon3d_ffmpeg_lib_dir}" NO_DEFAULT_PATH)

    if(NOT FFMPEG_AVCODEC_INCLUDE_DIR OR NOT FFMPEG_AVCODEC_LIBRARY)
        message(FATAL_ERROR "FFmpeg development files not found")
    endif()

    add_library(chronon3d_ffmpeg INTERFACE)
    target_include_directories(chronon3d_ffmpeg INTERFACE "${FFMPEG_AVCODEC_INCLUDE_DIR}")
    target_link_libraries(chronon3d_ffmpeg INTERFACE
        "${FFMPEG_AVCODEC_LIBRARY}"
        "${FFMPEG_AVFORMAT_LIBRARY}"
        "${FFMPEG_AVUTIL_LIBRARY}"
        "${FFMPEG_SWSCALE_LIBRARY}"
    )
endif()

# 1. Interface Library
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/include>
)
target_link_libraries(chronon3d INTERFACE glm::glm meshoptimizer::meshoptimizer xxHash::xxhash)
if(MSVC)
    target_compile_options(chronon3d INTERFACE /EHsc /fp:strict /utf-8)
    target_compile_definitions(chronon3d INTERFACE _DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION)
else()
    target_compile_options(chronon3d INTERFACE -ffp-contract=off)
endif()


# 3. Registry
file(GLOB CHRONON3D_REGISTRY_SOURCES CONFIGURE_DEPENDS "src/registry/*.cpp")
add_library(chronon3d_registry STATIC ${CHRONON3D_REGISTRY_SOURCES})
target_link_libraries(chronon3d_registry PUBLIC chronon3d)

# 4. Cache
file(GLOB CHRONON3D_CACHE_SOURCES CONFIGURE_DEPENDS "src/cache/*.cpp")
add_library(chronon3d_cache STATIC ${CHRONON3D_CACHE_SOURCES})
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash)

# 5. Render Graph
file(GLOB CHRONON3D_GRAPH_SOURCES CONFIGURE_DEPENDS 
    "src/render_graph/*.cpp"
)
add_library(chronon3d_graph OBJECT ${CHRONON3D_GRAPH_SOURCES})
target_link_libraries(chronon3d_graph PUBLIC chronon3d chronon3d_cache fmt::fmt hwy::hwy tomlplusplus::tomlplusplus)

# 6. Effects
file(GLOB CHRONON3D_EFFECTS_SOURCES CONFIGURE_DEPENDS "src/effects/*.cpp")
add_library(chronon3d_effects STATIC ${CHRONON3D_EFFECTS_SOURCES})
target_link_libraries(chronon3d_effects PUBLIC chronon3d meshoptimizer::meshoptimizer)

# 6b. Runtime
file(GLOB CHRONON3D_RUNTIME_SOURCES CONFIGURE_DEPENDS "src/runtime/*.cpp")
add_library(chronon3d_runtime STATIC ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC
    chronon3d chronon3d_graph chronon3d_effects chronon3d_cache
    Taskflow::Taskflow
)

# 7. Backends

# 7a. Image Backend (Replacement for IO)
file(GLOB CHRONON3D_BACKEND_IMAGE_SOURCES CONFIGURE_DEPENDS "src/backends/image/*.cpp")
add_library(chronon3d_backend_image STATIC ${CHRONON3D_BACKEND_IMAGE_SOURCES})
target_link_libraries(chronon3d_backend_image PUBLIC chronon3d spdlog::spdlog fmt::fmt)
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})

# 7b. Assets Backend
file(GLOB CHRONON3D_BACKEND_ASSETS_SOURCES CONFIGURE_DEPENDS "src/backends/assets/*.cpp")
add_library(chronon3d_backend_assets STATIC ${CHRONON3D_BACKEND_ASSETS_SOURCES})
target_link_libraries(chronon3d_backend_assets PUBLIC chronon3d chronon3d_backend_image)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})

# 7c. Text Backend
file(GLOB CHRONON3D_BACKEND_TEXT_SOURCES CONFIGURE_DEPENDS "src/backends/text/*.cpp")
add_library(chronon3d_backend_text STATIC ${CHRONON3D_BACKEND_TEXT_SOURCES})
target_link_libraries(chronon3d_backend_text PUBLIC chronon3d chronon3d_backend_assets)
target_include_directories(chronon3d_backend_text PRIVATE ${Stb_INCLUDE_DIR})

# 7d. Video Backend (Generic)
file(GLOB CHRONON3D_BACKEND_VIDEO_SOURCES CONFIGURE_DEPENDS "src/backends/video/*.cpp")
add_library(chronon3d_backend_video STATIC ${CHRONON3D_BACKEND_VIDEO_SOURCES})
target_link_libraries(chronon3d_backend_video PUBLIC chronon3d)

# 7e. FFmpeg Backend (Optional)
if(CHRONON3D_ENABLE_VIDEO)
    file(GLOB CHRONON3D_BACKEND_FFMPEG_SOURCES CONFIGURE_DEPENDS "src/backends/ffmpeg/*.cpp")
    add_library(chronon3d_backend_ffmpeg STATIC ${CHRONON3D_BACKEND_FFMPEG_SOURCES})
    target_link_libraries(chronon3d_backend_ffmpeg 
        PUBLIC 
            chronon3d 
            chronon3d_backend_video 
            chronon3d_backend_image
            chronon3d_ffmpeg 
            xxHash::xxhash 
            fmt::fmt 
            spdlog::spdlog
    )
    target_compile_definitions(chronon3d_backend_ffmpeg PUBLIC CHRONON_WITH_VIDEO)
endif()

# 7f. Software Renderer Backend
file(GLOB CHRONON3D_BACKEND_SOFTWARE_SOURCES CONFIGURE_DEPENDS 
    "src/backends/software/*.cpp"
    "src/backends/software/rasterizers/*.cpp"
    "src/backends/software/specialized/*.cpp"
    "src/backends/software/utils/*.cpp"
)
add_library(chronon3d_backend_software OBJECT ${CHRONON3D_BACKEND_SOFTWARE_SOURCES})
target_link_libraries(chronon3d_backend_software
    PUBLIC 
        chronon3d chronon3d_graph chronon3d_cache chronon3d_effects chronon3d_runtime
        chronon3d_backend_text chronon3d_backend_assets chronon3d_backend_image
    PRIVATE spdlog::spdlog hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR})

# 8. Scene Assembly (Scene, Layout, SpecScene)
file(GLOB CHRONON3D_SCENE_SOURCES CONFIGURE_DEPENDS
    "src/scene/*.cpp"
    "src/layout/*.cpp"
    "src/specscene/*.cpp"
)
add_library(chronon3d_scene OBJECT ${CHRONON3D_SCENE_SOURCES})
target_link_libraries(chronon3d_scene
    PUBLIC 
        chronon3d chronon3d_runtime chronon3d_graph chronon3d_backend_software chronon3d_registry 
        chronon3d_cache chronon3d_effects chronon3d_backend_image chronon3d_backend_video
    PRIVATE spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus
)

if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_scene PUBLIC chronon3d_backend_ffmpeg)
    target_compile_definitions(chronon3d_scene PUBLIC CHRONON_WITH_VIDEO)
endif()

# 11. Pipeline (Interface)
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE
    chronon3d_scene chronon3d_runtime chronon3d_graph chronon3d_backend_software chronon3d_registry chronon3d_cache
    Taskflow::Taskflow concurrentqueue::concurrentqueue
)

# Profiling
if(CHRONON3D_ENABLE_PROFILING AND Tracy_FOUND)
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

# Subdirectories
if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

if(CHRONON3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
