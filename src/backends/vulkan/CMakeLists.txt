find_program(CHRONON3D_GLSLANG_VALIDATOR glslangValidator REQUIRED)
set(CHRONON3D_VULKAN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CHRONON3D_COMPOSITE_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/composite.comp.spv")
set(CHRONON3D_COMPOSITE_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/composite_comp_spv.hpp")
set(CHRONON3D_TRANSFORM_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/transform.comp.spv")
set(CHRONON3D_TRANSFORM_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/transform_comp_spv.hpp")
set(CHRONON3D_AFFINE_TRANSFORM_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/affine_transform.comp.spv")
set(CHRONON3D_AFFINE_TRANSFORM_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/affine_transform_comp_spv.hpp")
set(CHRONON3D_BLUR_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/blur.comp.spv")
set(CHRONON3D_BLUR_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/blur_comp_spv.hpp")
set(CHRONON3D_COLOR_ADJUST_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/color_adjust.comp.spv")
set(CHRONON3D_COLOR_ADJUST_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/color_adjust_comp_spv.hpp")
set(CHRONON3D_MATTE_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/matte.comp.spv")
set(CHRONON3D_MATTE_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/matte_comp_spv.hpp")
set(CHRONON3D_TEXT_RUN_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/text_run.comp.spv")
set(CHRONON3D_TEXT_RUN_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/text_run_comp_spv.hpp")
set(CHRONON3D_FILL_RECT_SPV "${CHRONON3D_VULKAN_GENERATED_DIR}/fill_rect.comp.spv")
set(CHRONON3D_FILL_RECT_HEADER "${CHRONON3D_VULKAN_GENERATED_DIR}/fill_rect_comp_spv.hpp")
add_custom_command(
    OUTPUT "${CHRONON3D_COMPOSITE_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/composite.comp" -o "${CHRONON3D_COMPOSITE_SPV}"
    DEPENDS shaders/composite.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_COMPOSITE_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_COMPOSITE_SPV} -DOUTPUT=${CHRONON3D_COMPOSITE_HEADER} -DSYMBOL=chronon3d_composite_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_COMPOSITE_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_TRANSFORM_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/transform.comp" -o "${CHRONON3D_TRANSFORM_SPV}"
    DEPENDS shaders/transform.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_TRANSFORM_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_TRANSFORM_SPV} -DOUTPUT=${CHRONON3D_TRANSFORM_HEADER} -DSYMBOL=chronon3d_transform_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_TRANSFORM_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_AFFINE_TRANSFORM_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/affine_transform.comp" -o "${CHRONON3D_AFFINE_TRANSFORM_SPV}"
    DEPENDS shaders/affine_transform.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_AFFINE_TRANSFORM_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_AFFINE_TRANSFORM_SPV} -DOUTPUT=${CHRONON3D_AFFINE_TRANSFORM_HEADER} -DSYMBOL=chronon3d_affine_transform_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_AFFINE_TRANSFORM_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_BLUR_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/blur.comp" -o "${CHRONON3D_BLUR_SPV}"
    DEPENDS shaders/blur.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_BLUR_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_BLUR_SPV} -DOUTPUT=${CHRONON3D_BLUR_HEADER} -DSYMBOL=chronon3d_blur_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_BLUR_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_COLOR_ADJUST_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_adjust.comp" -o "${CHRONON3D_COLOR_ADJUST_SPV}"
    DEPENDS shaders/color_adjust.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_COLOR_ADJUST_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_COLOR_ADJUST_SPV} -DOUTPUT=${CHRONON3D_COLOR_ADJUST_HEADER} -DSYMBOL=chronon3d_color_adjust_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_COLOR_ADJUST_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_MATTE_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/matte.comp" -o "${CHRONON3D_MATTE_SPV}"
    DEPENDS shaders/matte.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_MATTE_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_MATTE_SPV} -DOUTPUT=${CHRONON3D_MATTE_HEADER} -DSYMBOL=chronon3d_matte_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_MATTE_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_TEXT_RUN_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/text_run.comp" -o "${CHRONON3D_TEXT_RUN_SPV}"
    DEPENDS shaders/text_run.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_TEXT_RUN_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_TEXT_RUN_SPV} -DOUTPUT=${CHRONON3D_TEXT_RUN_HEADER} -DSYMBOL=chronon3d_text_run_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_TEXT_RUN_SPV}" embed_spirv.cmake VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_FILL_RECT_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
    COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fill_rect.comp" -o "${CHRONON3D_FILL_RECT_SPV}"
    DEPENDS shaders/fill_rect.comp VERBATIM)
add_custom_command(
    OUTPUT "${CHRONON3D_FILL_RECT_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CHRONON3D_FILL_RECT_SPV} -DOUTPUT=${CHRONON3D_FILL_RECT_HEADER} -DSYMBOL=chronon3d_fill_rect_comp_spv -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
    DEPENDS "${CHRONON3D_FILL_RECT_SPV}" embed_spirv.cmake VERBATIM)
add_library(chronon3d_backend_vulkan OBJECT vulkan_backend.cpp
    vulkan_legacy_fallback.cpp
    "${CHRONON3D_COMPOSITE_HEADER}" "${CHRONON3D_TRANSFORM_HEADER}"
    "${CHRONON3D_AFFINE_TRANSFORM_HEADER}" "${CHRONON3D_BLUR_HEADER}"
    "${CHRONON3D_COLOR_ADJUST_HEADER}" "${CHRONON3D_MATTE_HEADER}"
    "${CHRONON3D_TEXT_RUN_HEADER}" "${CHRONON3D_FILL_RECT_HEADER}")
set_target_properties(chronon3d_backend_vulkan PROPERTIES UNITY_BUILD OFF)
target_compile_definitions(chronon3d_backend_vulkan PUBLIC CHRONON3D_ENABLE_VULKAN)
target_link_libraries(chronon3d_backend_vulkan PUBLIC chronon3d Vulkan::Vulkan)
target_include_directories(chronon3d_backend_vulkan
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CHRONON3D_VULKAN_GENERATED_DIR}>
    PRIVATE ${CMAKE_SOURCE_DIR}
)

if(CHRONON3D_ENABLE_CUDA_INTEROP)
    foreach(_chronon3d_cuda_cache_var IN ITEMS
            CHRONON3D_CUDA_INCLUDE_DIR
            CHRONON3D_CUDA_DRIVER_LIBRARY
            CHRONON3D_NVRTC_INCLUDE_DIR
            CHRONON3D_NVRTC_LIBRARY)
        if(DEFINED ${_chronon3d_cuda_cache_var} AND
           "${${_chronon3d_cuda_cache_var}}" MATCHES
           "(/usr/local/lib/|/tmp/)|libnvrtc(-builtins)?\\.so\\.[0-9]+")
            unset(${_chronon3d_cuda_cache_var} CACHE)
            unset(${_chronon3d_cuda_cache_var})
        endif()
    endforeach()
    find_path(CHRONON3D_CUDA_INCLUDE_DIR cuda.h
        HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
        PATH_SUFFIXES include)
    find_library(CHRONON3D_CUDA_DRIVER_LIBRARY cuda
        HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
        PATH_SUFFIXES lib64 lib/x64 lib)
    if(NOT CHRONON3D_CUDA_INCLUDE_DIR OR NOT CHRONON3D_CUDA_DRIVER_LIBRARY)
        message(FATAL_ERROR
            "CHRONON3D_ENABLE_CUDA_INTEROP requires cuda.h and libcuda")
    endif()
    # Publish the resolved values to sibling consumers (CLI/media) through
    # the CMake cache; directory-scoped variables do not cross subdirectories.
    set(CHRONON3D_CUDA_INCLUDE_DIR "${CHRONON3D_CUDA_INCLUDE_DIR}" CACHE PATH
        "Canonical CUDA include directory" FORCE)
    set(CHRONON3D_CUDA_DRIVER_LIBRARY "${CHRONON3D_CUDA_DRIVER_LIBRARY}" CACHE FILEPATH
        "Canonical CUDA driver library" FORCE)

    target_sources(chronon3d_backend_vulkan PRIVATE
        cuda_vulkan_surface_bridge.cpp)
    target_include_directories(chronon3d_backend_vulkan
        PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
    target_link_libraries(chronon3d_backend_vulkan
        PUBLIC "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

    if(TARGET chronon3d_media_native)
        target_include_directories(chronon3d_media_native PRIVATE
            "${CHRONON3D_CUDA_INCLUDE_DIR}")
        target_link_libraries(chronon3d_media_native PRIVATE
            chronon3d_backend_vulkan "${CHRONON3D_CUDA_DRIVER_LIBRARY}")
    endif()

    add_executable(chronon3d_cuda_vulkan_external_memory_probe
        "${CMAKE_SOURCE_DIR}/tools/cuda_vulkan_external_memory_probe.cpp"
        cuda_vulkan_surface_bridge.cpp)
    target_include_directories(chronon3d_cuda_vulkan_external_memory_probe
        PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/include"
                "${CMAKE_SOURCE_DIR}/vcpkg_installed/linux-fast-dev/x64-linux/include")
    target_link_libraries(chronon3d_cuda_vulkan_external_memory_probe
        PRIVATE chronon3d Vulkan::Vulkan "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        add_executable(chronon3d_cuda_vulkan_nvenc_probe
            "${CMAKE_SOURCE_DIR}/tools/cuda_vulkan_nvenc_probe.cpp")
        target_include_directories(chronon3d_cuda_vulkan_nvenc_probe
            PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
        target_link_libraries(chronon3d_cuda_vulkan_nvenc_probe
            PRIVATE chronon3d_ffmpeg_full Vulkan::Vulkan
                    "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

        find_path(CHRONON3D_NVRTC_INCLUDE_DIR nvrtc.h
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
            PATH_SUFFIXES include include/cuda)
        find_library(CHRONON3D_NVRTC_LIBRARY
            NAMES nvrtc libnvrtc.so
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
            PATH_SUFFIXES lib64 lib lib/x64)
        # Some CUDA distributions (notably Python wheels) ship only a
        # versioned NVRTC shared object and omit the development symlink.
        if(NOT CHRONON3D_NVRTC_LIBRARY)
            foreach(_chronon3d_cuda_root IN ITEMS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}")
                if(_chronon3d_cuda_root)
                    foreach(_chronon3d_cuda_libdir IN ITEMS lib64 lib lib/x64)
                        file(GLOB _chronon3d_nvrtc_candidates
                            "${_chronon3d_cuda_root}/${_chronon3d_cuda_libdir}/libnvrtc.so.*")
                        if(_chronon3d_nvrtc_candidates)
                            list(SORT _chronon3d_nvrtc_candidates COMPARE NATURAL ORDER DESCENDING)
                            list(GET _chronon3d_nvrtc_candidates 0 CHRONON3D_NVRTC_LIBRARY)
                            break()
                        endif()
                    endforeach()
                endif()
                if(CHRONON3D_NVRTC_LIBRARY)
                    break()
                endif()
            endforeach()
        endif()
        set(CHRONON3D_NVRTC_INCLUDE_DIR "${CHRONON3D_NVRTC_INCLUDE_DIR}" CACHE PATH
            "Canonical NVRTC include directory" FORCE)
        set(CHRONON3D_NVRTC_LIBRARY "${CHRONON3D_NVRTC_LIBRARY}" CACHE FILEPATH
            "Canonical NVRTC library" FORCE)
        if(TARGET chronon3d_media_native)
            if(NOT CHRONON3D_NVRTC_INCLUDE_DIR OR NOT CHRONON3D_NVRTC_LIBRARY)
                message(FATAL_ERROR
                    "CHRONON3D_ENABLE_CUDA_INTEROP with native FFmpeg requires NVRTC")
            endif()
            target_include_directories(chronon3d_media_native PRIVATE
                "${CHRONON3D_NVRTC_INCLUDE_DIR}")
            target_link_libraries(chronon3d_media_native PRIVATE
                "${CHRONON3D_NVRTC_LIBRARY}")
        endif()
        if(CHRONON3D_NVRTC_INCLUDE_DIR AND CHRONON3D_NVRTC_LIBRARY)
            add_executable(chronon3d_cuda_nvdec_nvenc_overlay_bench
                "${CMAKE_SOURCE_DIR}/tools/cuda_nvdec_nvenc_overlay_bench.cpp")
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
                target_compile_definitions(chronon3d_cuda_nvdec_nvenc_overlay_bench PRIVATE CHRONON3D_ENABLE_RAW_NVENC)
                target_include_directories(chronon3d_cuda_nvdec_nvenc_overlay_bench PRIVATE "${CHRONON3D_NV_CODEC_INCLUDE_DIR}")
                target_link_libraries(chronon3d_cuda_nvdec_nvenc_overlay_bench PRIVATE "${CHRONON3D_NVENCODE_LIBRARY}" dl)
            endif()
        endif()
    endif()
endif()
