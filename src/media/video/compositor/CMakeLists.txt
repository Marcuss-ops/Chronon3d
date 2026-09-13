add_library(chronon3d_video_compositor OBJECT
    cuda/cuda_nv12_compositor.cpp
    cuda/cuda_nv12_compositor_paths.cpp
    cuda/cuda_nv12_compositor_batch.cpp)
set_target_properties(chronon3d_video_compositor PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_video_compositor PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(chronon3d_video_compositor PUBLIC chronon3d chronon3d_cache)
if(CHRONON3D_ENABLE_CUDA_INTEROP)
    target_compile_definitions(chronon3d_video_compositor PRIVATE CHRONON3D_ENABLE_CUDA_INTEROP)
    target_include_directories(chronon3d_video_compositor PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR}" "${CHRONON3D_NVRTC_INCLUDE_DIR}")
    target_link_libraries(chronon3d_video_compositor PRIVATE
        "${CHRONON3D_CUDA_DRIVER_LIBRARY}" "${CHRONON3D_NVRTC_LIBRARY}")
endif()
