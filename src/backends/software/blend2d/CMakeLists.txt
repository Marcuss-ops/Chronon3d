# ==============================================================================
# src/backends/software/blend2d/CMakeLists.txt
#
# PR4: introduces the BL2-fill orchestration helper.  Sources here only
# compile when Blend2D is available (CHRONON3D_USE_BLEND2D=ON), since
# the CoverageSurface wraps BLImage and the filler compiles against
# <blend2d.h>.  When the option is OFF the parent target continues to
# build with the legacy PIP/custom rasterizer unchanged.
# ==============================================================================

if(CHRONON3D_USE_BLEND2D)
    target_sources(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/coverage_surface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/blend2d_path_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/blend2d_path_filler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/blend2d_stroke_options.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/blend2d_path_stroker.cpp
    )
    target_include_directories(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
