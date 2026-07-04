# ==============================================================================
# src/backends/image/CMakeLists.txt — chronon3d_backend_image
# ==============================================================================

add_library(chronon3d_backend_image OBJECT
    image_loader.cpp
    image_writer.cpp
    stb_image_backend.cpp)

if(CHRONON3D_ENABLE_EXR)
    target_sources(chronon3d_backend_image PRIVATE exr_mmap.cpp)
endif()

# M1.5#7 — stb_image.h / stb_image_write.h single-header expansion OOMs gcc
# even with -j1 on machines with limited RAM. The two TUs that define the
# STB_IMAGE(_WRITE)_IMPLEMENTATION macros contain the full decoder/encoder code
# (~8000 LOC + ~1700 LOC). Compile them with reduced optimization flags and
# skip the PCH (which is not compatible with these flags) and unity build
# inclusion (consolidated TUs would re-enable -O2 from the target's flags).
# No runtime impact: stb parse/encode is I/O bound, not inner-loop bound.
set_source_files_properties(stb_image_backend.cpp image_writer.cpp
    PROPERTIES
        COMPILE_OPTIONS "-O0;-fno-var-tracking;-g0;-pipe"
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)

target_link_libraries(chronon3d_backend_image PUBLIC
    chronon3d
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_EXR)
    target_link_libraries(chronon3d_backend_image PUBLIC OpenEXR::OpenEXR)
endif()
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(chronon3d_backend_image PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_backend_image PRIVATE <chronon3d/detail/pch.hpp>)
