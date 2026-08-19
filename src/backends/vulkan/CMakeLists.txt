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

    target_sources(chronon3d_backend_vulkan PRIVATE
        cuda_vulkan_surface_bridge.cpp)
    target_include_directories(chronon3d_backend_vulkan
        PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
    target_link_libraries(chronon3d_backend_vulkan
        PUBLIC "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

    add_executable(chronon3d_cuda_vulkan_external_memory_probe
        "${CMAKE_SOURCE_DIR}/tools/cuda_vulkan_external_memory_probe.cpp"
        cuda_vulkan_surface_bridge.cpp)
    target_include_directories(chronon3d_cuda_vulkan_external_memory_probe
        PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/include"
                "${CMAKE_SOURCE_DIR}/vcpkg_installed/linux-fast-dev/x64-linux/include")
    target_link_libraries(chronon3d_cuda_vulkan_external_memory_probe
        PRIVATE Vulkan::Vulkan "${CHRONON3D_CUDA_DRIVER_LIBRARY}")

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        add_executable(chronon3d_cuda_vulkan_nvenc_probe
            "${CMAKE_SOURCE_DIR}/tools/cuda_vulkan_nvenc_probe.cpp")
        target_include_directories(chronon3d_cuda_vulkan_nvenc_probe
            PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
        target_link_libraries(chronon3d_cuda_vulkan_nvenc_probe
            PRIVATE chronon3d_ffmpeg_full Vulkan::Vulkan
                    "${CHRONON3D_CUDA_DRIVER_LIBRARY}")
    endif()
endif()
