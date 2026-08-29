# Chronon CUDA discovery shared by CUDA video media and the optional Vulkan
# interop backend.  CUDA video primitives must not depend on Vulkan merely to
# find the driver/NVRTC libraries.

function(chronon3d_configure_cuda)
    foreach(_chronon3d_cuda_cache_var IN ITEMS
            CHRONON3D_CUDA_INCLUDE_DIR
            CHRONON3D_CUDA_DRIVER_LIBRARY
            CHRONON3D_NVRTC_INCLUDE_DIR
            CHRONON3D_NVRTC_LIBRARY)
        if(DEFINED ${_chronon3d_cuda_cache_var} AND
           "${${_chronon3d_cuda_cache_var}}" MATCHES
           "(/tmp/)|libnvrtc(-builtins)?\\.so\\.[0-9]+")
            unset(${_chronon3d_cuda_cache_var} CACHE)
            unset(${_chronon3d_cuda_cache_var})
        endif()
    endforeach()

    find_path(CHRONON3D_CUDA_INCLUDE_DIR cuda.h
        HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "/usr/local/cuda"
              "/home/pierone/cuda-include-chronon"
        PATH_SUFFIXES include)
    find_library(CHRONON3D_CUDA_DRIVER_LIBRARY cuda
        HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
              "/usr/lib/x86_64-linux-gnu"
        PATH_SUFFIXES lib64 lib/x64 lib)
    if(NOT CHRONON3D_CUDA_INCLUDE_DIR OR NOT CHRONON3D_CUDA_DRIVER_LIBRARY)
        message(FATAL_ERROR
            "CHRONON3D_ENABLE_CUDA_INTEROP requires cuda.h and libcuda")
    endif()
    set(CHRONON3D_CUDA_INCLUDE_DIR "${CHRONON3D_CUDA_INCLUDE_DIR}" CACHE PATH
        "Canonical CUDA include directory" FORCE)
    set(CHRONON3D_CUDA_DRIVER_LIBRARY "${CHRONON3D_CUDA_DRIVER_LIBRARY}" CACHE FILEPATH
        "Canonical CUDA driver library" FORCE)

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        find_path(CHRONON3D_NVRTC_INCLUDE_DIR nvrtc.h
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "/usr/local/cuda"
                  "/home/pierone/cuda-include-chronon"
            PATH_SUFFIXES include include/cuda)
        find_library(CHRONON3D_NVRTC_LIBRARY
            NAMES nvrtc libnvrtc.so libnvrtc-chronon.so
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "/usr/local/cuda"
                  "/usr/local/cuda-13.0" "/home/pierone"
            PATH_SUFFIXES lib64 lib lib/x64 targets/x86_64-linux/lib)
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
        if(NOT CHRONON3D_NVRTC_INCLUDE_DIR OR NOT CHRONON3D_NVRTC_LIBRARY)
            message(FATAL_ERROR
                "CHRONON3D_ENABLE_CUDA_INTEROP with native FFmpeg requires NVRTC")
        endif()
    endif()
endfunction()
