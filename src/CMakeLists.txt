# src/CMakeLists.txt

# Render-plan validation is a first-class Chronon contract, independent of IPC.
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    # Native FFmpeg is a build contract, not a best-effort linker fallback.
    # Any preset that opts into the in-process backend must fail during
    # configure when the required headers or libraries are unavailable.
    find_path(CHRONON3D_FFMPEG_INCLUDE_DIR
        NAMES libavcodec/avcodec.h
        REQUIRED)
    find_library(CHRONON3D_FFMPEG_SWSCALE_LIBRARY
        NAMES swscale
        REQUIRED)
    find_library(CHRONON3D_FFMPEG_AVCODEC_LIBRARY
        NAMES avcodec
        REQUIRED)
    find_library(CHRONON3D_FFMPEG_AVFORMAT_LIBRARY
        NAMES avformat
        REQUIRED)
    find_library(CHRONON3D_FFMPEG_AVUTIL_LIBRARY
        NAMES avutil
        REQUIRED)
    # Required, not optional: the audio resample stage (AudioResampler) is the
    # only authority that can honour a declared output sample rate, and a
    # build without it would silently reintroduce source-rate audio.
    find_library(CHRONON3D_FFMPEG_SWRESAMPLE_LIBRARY
        NAMES swresample
        REQUIRED)

    add_library(chronon3d_ffmpeg_light INTERFACE)
    target_include_directories(chronon3d_ffmpeg_light INTERFACE
        "${CHRONON3D_FFMPEG_INCLUDE_DIR}")
    target_link_libraries(chronon3d_ffmpeg_light INTERFACE
        "${CHRONON3D_FFMPEG_SWSCALE_LIBRARY}"
        "${CHRONON3D_FFMPEG_AVUTIL_LIBRARY}")

    add_library(chronon3d_ffmpeg_full INTERFACE)
    target_include_directories(chronon3d_ffmpeg_full INTERFACE
        "${CHRONON3D_FFMPEG_INCLUDE_DIR}")
    target_link_libraries(chronon3d_ffmpeg_full INTERFACE
        "${CHRONON3D_FFMPEG_SWSCALE_LIBRARY}"
        "${CHRONON3D_FFMPEG_SWRESAMPLE_LIBRARY}"
        "${CHRONON3D_FFMPEG_AVCODEC_LIBRARY}"
        "${CHRONON3D_FFMPEG_AVFORMAT_LIBRARY}"
        "${CHRONON3D_FFMPEG_AVUTIL_LIBRARY}")
endif()

add_subdirectory(core)

# Base engine boundary must exist before every subsystem that consumes it.
add_library(chronon3d_base INTERFACE)
target_include_directories(chronon3d_base INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(chronon3d_base INTERFACE
    glm::glm xxHash::xxhash magic_enum::magic_enum)

add_library(chronon3d INTERFACE)
target_link_libraries(chronon3d INTERFACE chronon3d_base chronon3d_core_impl)

add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE chronon3d_core_impl)
add_subdirectory(cache)
add_subdirectory(effects)
add_subdirectory(registry)
add_subdirectory(assets)
add_subdirectory(backends/image)
add_subdirectory(media)
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D)
    add_subdirectory(backends/text)
endif()
add_subdirectory(backends/assets)
if(CHRONON3D_ENABLE_VULKAN)
    add_subdirectory(backends/vulkan)
endif()
add_subdirectory(render_graph)
add_subdirectory(runtime)
if(CHRONON3D_ENABLE_VERIFICATION)
    add_subdirectory(verification)
endif()
add_subdirectory(render_plan)
add_subdirectory(compiler)
add_subdirectory(extension)
add_subdirectory(backends/software)
add_subdirectory(scene)
add_subdirectory(timeline)
add_subdirectory(animations)
if(CHRONON3D_ENABLE_TEXT)
    add_subdirectory(text)
endif()
if(CHRONON3D_ENABLE_IPC)
    add_subdirectory(ipc)
endif()

# Phase 12 (CMake DAG demolition): the former `chronon3d_software` INTERFACE
# aggregate was semantically dishonest — a target named "software" that pulled
# in the Vulkan backend and the GPU video backends whenever they were enabled.
# It has been demolished: the pipeline aggregate now lists the semantic backend
# capability boundaries directly. A `chronon3d_software` facade must not be
# recreated (enforced by the cmake_dag forbidden_target rule).
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE
    chronon3d_core
    chronon3d_backend_software
    chronon3d_backend_assets
    chronon3d_backend_image
)
if(CHRONON3D_ENABLE_VULKAN)
    target_link_libraries(chronon3d_pipeline INTERFACE chronon3d_backend_vulkan)
endif()
if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_pipeline INTERFACE
        chronon3d_backend_video chronon3d_media_video)
endif()
if(TARGET chronon3d_text_mtsdf)
    # gpu_text_run.cpp is part of the graph object set, while the shared
    # FreeType MTSDF generator is a STATIC leaf. Keep the archive on the
    # final pipeline boundary so Vulkan-only consumers resolve it as well.
    target_link_libraries(chronon3d_pipeline INTERFACE chronon3d_text_mtsdf)
endif()

# `chronon3d_assets` is an OBJECT library, so its private OpenSSL dependency
# does not reach final consumers through the SDK aggregate.  Publish the
# crypto runtime at the pipeline boundary when the asset digest uses EVP.
find_package(OpenSSL QUIET COMPONENTS Crypto)
if(OpenSSL_FOUND)
    target_link_libraries(chronon3d_pipeline INTERFACE OpenSSL::Crypto)
else()
    find_library(CHRONON3D_CRYPTO_LIBRARY
        NAMES crypto libcrypto.so.3 libcrypto.so)
    if(CHRONON3D_CRYPTO_LIBRARY)
        target_link_libraries(chronon3d_pipeline INTERFACE
            "${CHRONON3D_CRYPTO_LIBRARY}")
    endif()
endif()

# `chronon3d_render_plan` is an OBJECT library with a PRIVATE
# nlohmann_json_schema_validator dependency; its object files reach the
# build interface through the registry exposure below, so the validator
# runtime must be published at the pipeline boundary unconditionally
# (the render-plan validator is core surface, not IPC-gated).
target_link_libraries(chronon3d_pipeline INTERFACE
    nlohmann_json_schema_validator::validator)

chronon3d_expose_registered_objects_to_build_interface(chronon3d_pipeline)

if(CHRONON3D_ENABLE_VULKAN AND CHRONON3D_ENABLE_CUDA_INTEROP
   AND CHRONON3D_ENABLE_NATIVE_FFMPEG
   AND CHRONON3D_NVRTC_INCLUDE_DIR AND CHRONON3D_NVRTC_LIBRARY)
    # The Vulkan backend is an OBJECT library, so publish NVRTC through the
    # canonical pipeline interface after that target has been created.
    target_link_libraries(chronon3d_pipeline INTERFACE
        "${CHRONON3D_NVRTC_LIBRARY}")
endif()

# DirectYUV depends on the completed engine/media graph but is not itself an
# SDK aggregate component. Define its stable facade for every video build;
# native FFmpeg/CUDA sources are selected inside the subdirectory target.
if(CHRONON3D_ENABLE_VIDEO)
    add_subdirectory(media/video/direct_yuv)
endif()

# Define the consumer-facing SDK targets after every registry-backed component
# has registered its object libraries, and before the root install/export pass.
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DSdkTargets.cmake)

if(MSVC AND CHRONON3D_PARALLEL_MSVC)
    file(GLOB_RECURSE CHRONON3D_SRC_CPP CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    if(CHRONON3D_SRC_CPP)
        set_source_files_properties(${CHRONON3D_SRC_CPP} PROPERTIES COMPILE_OPTIONS "/MP")
    endif()
endif()
