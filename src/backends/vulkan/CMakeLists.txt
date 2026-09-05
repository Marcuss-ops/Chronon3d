# ── Vulkan shader compilation (canonical pipeline) ─────────────────────────
# Uses cmake/ChrononShaders.cmake: compile → validate → optimize → embed

set(CHRONON3D_VULKAN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated" CACHE INTERNAL "Vulkan shader generated output directory")

# Enumerate all compute shaders
set(_chronon3d_shaders
    composite
    transform
    affine_transform
    blur
    color_adjust
    matte
    text_run
    fill_rect
    layer_batch
    text_batch
    text_tile_bin
    text_tile_raster
)

foreach(_shader ${_chronon3d_shaders})
    chronon_add_compute_shader(
        NAME   ${_shader}
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_shader}.comp"
    )
endforeach()

# Collect all generated headers for the library target
set(_chronon3d_shader_headers "")
foreach(_shader ${_chronon3d_shaders})
    list(APPEND _chronon3d_shader_headers "${chronon3d_${_shader}_header}")
endforeach()

# ── SPIR-V ABI codegen (SPIRV-Reflect) ─────────────────────────────────────
# spirv_abi_gen reflects each final .spv into a typed C++ ABI header.  These
# headers are the canonical "what the shader actually declares" metadata that
# the ABI-consistency test cross-checks against the backend's hardcoded
# bindings (tests/backends/test_shader_abi_consistency.cpp).
set(_chronon3d_shader_abi_prefixes
    composite:Composite
    transform:Transform
    affine_transform:AffineTransform
    blur:Blur
    color_adjust:ColorAdjust
    matte:Matte
    text_run:TextRun
    fill_rect:FillRect
    layer_batch:LayerBatch
    text_batch:TextBatch
    text_tile_bin:TextTileBin
    text_tile_raster:TextTileRaster
)

set(_chronon3d_shader_abi_headers "")
foreach(_pair ${_chronon3d_shader_abi_prefixes})
    string(REPLACE ":" ";" _kv "${_pair}")
    list(GET _kv 0 _shader)
    list(GET _kv 1 _prefix)
    set(_abi_header "${CHRONON3D_VULKAN_GENERATED_DIR}/${_shader}_abi.hpp")
    add_custom_command(
        OUTPUT "${_abi_header}"
        COMMAND $<TARGET_FILE:spirv_abi_gen>
            "${chronon3d_${_shader}_spv}"
            "${_abi_header}"
            "${_prefix}"
        DEPENDS spirv_abi_gen "${chronon3d_${_shader}_spv}"
        COMMENT "Reflecting ${_shader}.comp SPIR-V → ${_shader}_abi.hpp"
        VERBATIM
    )
    list(APPEND _chronon3d_shader_abi_headers "${_abi_header}")
endforeach()

# Aggregate target so the codegen tool + ABI headers build on demand.
add_custom_target(chronon3d_shader_abi_headers
    DEPENDS ${_chronon3d_shader_abi_headers}
)
set(CHRONON3D_VULKAN_ABI_HEADERS "${_chronon3d_shader_abi_headers}" CACHE INTERNAL
    "Vulkan shader ABI reflection headers")

# ── Vulkan backend library ────────────────────────────────────────────────
# VulkanBackend::Impl is declared in vulkan_backend_impl.hpp (private, not
# installed) and each implementation fragment compiles as an INDEPENDENT
# translation unit, so incremental rebuilds only recompile the touched
# fragment instead of the whole 5k-line backend.
add_library(chronon3d_backend_vulkan OBJECT
    vulkan_backend.cpp
    vulkan_backend_lifecycle.cpp
    vulkan_backend_stats.cpp
    vulkan_backend_surface_api.cpp
    vulkan_backend_lifecycle_private.cpp
    vulkan_descriptor_arena_private.cpp
    vulkan_surface_store_private.cpp
    vulkan_kernel_record_private.cpp
    vulkan_kernel_replay_private.cpp
    vulkan_backend_operations_private.cpp
    vulkan_backend_text_ops_private.cpp
    vulkan_backend_layer_ops_private.cpp
    vulkan_backend_resources_private.cpp
    vulkan_backend_submission_private.cpp
    vulkan_backend_sync_private.cpp
    vulkan_backend_cuda_export_private.cpp
    vulkan_native_shape_dispatch.cpp
    memory/vulkan_memory_manager.cpp
    debug/vulkan_debug_context.cpp
    ${_chronon3d_shader_headers}
)
set_target_properties(chronon3d_backend_vulkan PROPERTIES UNITY_BUILD OFF)
# Precompiled headers: the Vulkan/spdlog/profiling and common std headers are
# parsed once instead of once per fragment TU.
target_precompile_headers(chronon3d_backend_vulkan
    PRIVATE
        <vulkan/vulkan.h>
        <spdlog/spdlog.h>
        <chronon3d/core/profiling/profiling.hpp>
        <array>
        <chrono>
        <cstring>
        <memory>
        <mutex>
        <optional>
        <span>
        <stdexcept>
        <string>
        <unordered_map>
        <unordered_set>
        <utility>
        <vector>
)
target_compile_definitions(chronon3d_backend_vulkan PUBLIC CHRONON3D_ENABLE_VULKAN)
target_link_libraries(chronon3d_backend_vulkan
    PUBLIC
        chronon3d
        Vulkan::Vulkan
    PRIVATE
        GPUOpen::VulkanMemoryAllocator
)
target_include_directories(chronon3d_backend_vulkan
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CHRONON3D_VULKAN_GENERATED_DIR}>
    PRIVATE ${CMAKE_SOURCE_DIR}
)

# ── CUDA interop (optional) ───────────────────────────────────────────────
if(CHRONON3D_ENABLE_CUDA_INTEROP)
    target_sources(chronon3d_backend_vulkan PRIVATE
        cuda_vulkan_surface_bridge.cpp
        cuda_nv12_surface_compositor.cpp
        cuda_nv12_surface_compositor_paths.cpp
        cuda_nv12_surface_compositor_batch.cpp
        vulkan_cuda_frame_importer.cpp)
    target_include_directories(chronon3d_backend_vulkan
        PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
    target_link_libraries(chronon3d_backend_vulkan
        PUBLIC "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

    add_executable(chronon3d_cuda_vulkan_external_memory_probe
        "${CMAKE_SOURCE_DIR}/tools/hwlab/cuda_vulkan_external_memory_probe.cpp"
        cuda_vulkan_surface_bridge.cpp)
    target_include_directories(chronon3d_cuda_vulkan_external_memory_probe
        PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(chronon3d_cuda_vulkan_external_memory_probe
        PRIVATE chronon3d Vulkan::Vulkan "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        add_executable(chronon3d_cuda_vulkan_nvenc_probe
            "${CMAKE_SOURCE_DIR}/tools/hwlab/cuda_vulkan_nvenc_probe.cpp")
        target_include_directories(chronon3d_cuda_vulkan_nvenc_probe
            PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
        target_link_libraries(chronon3d_cuda_vulkan_nvenc_probe
            PRIVATE chronon3d_ffmpeg_full Vulkan::Vulkan
                    "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

        if(TARGET chronon3d_media_native)
            target_include_directories(chronon3d_media_native PRIVATE
                "${CHRONON3D_NVRTC_INCLUDE_DIR}")
            target_link_libraries(chronon3d_media_native PRIVATE
                "${CHRONON3D_NVRTC_LIBRARY}")
        endif()
        if(CHRONON3D_NVRTC_INCLUDE_DIR AND CHRONON3D_NVRTC_LIBRARY)
            target_link_libraries(chronon3d_backend_vulkan PUBLIC
                "${CHRONON3D_NVRTC_LIBRARY}")
            add_executable(chronon3d_cuda_nvdec_nvenc_overlay_bench
                "${CMAKE_SOURCE_DIR}/tools/hwlab/cuda_nvdec_nvenc_overlay_bench.cpp")
            target_include_directories(chronon3d_cuda_nvdec_nvenc_overlay_bench
                PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}"
                        "${CHRONON3D_NVRTC_INCLUDE_DIR}")
            target_link_libraries(chronon3d_cuda_nvdec_nvenc_overlay_bench
                PRIVATE chronon3d_ffmpeg_full
                        "${CHRONON3D_CUDA_DRIVER_LIBRARY}"
                        "${CHRONON3D_NVRTC_LIBRARY}")

            find_path(CHRONON3D_NV_CODEC_INCLUDE_DIR ffnvcodec/nvEncodeAPI.h
                HINTS "$ENV{NV_CODEC_HEADERS}" "$ENV{NV_CODEC_INCLUDE_DIR}"
                PATH_SUFFIXES include)
            find_library(CHRONON3D_NVENCODE_LIBRARY nvidia-encode
                HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
                PATH_SUFFIXES lib64 lib lib/x64)
            if(CHRONON3D_NV_CODEC_INCLUDE_DIR AND CHRONON3D_NVENCODE_LIBRARY)
                target_compile_definitions(chronon3d_cuda_nvdec_nvenc_overlay_bench
                    PRIVATE CHRONON3D_ENABLE_RAW_NVENC)
                target_include_directories(chronon3d_cuda_nvdec_nvenc_overlay_bench
                    PRIVATE "${CHRONON3D_NV_CODEC_INCLUDE_DIR}")
                target_link_libraries(chronon3d_cuda_nvdec_nvenc_overlay_bench
                    PRIVATE "${CHRONON3D_NVENCODE_LIBRARY}" dl)
            endif()
        endif()
    endif()
endif()
