# ==============================================================================
# src/backends/software/blend2d/CMakeLists.txt
# Single Blend2D authority: path-fill helpers, stroke options, the
# CoverageSurface, plus the blend2d_bridge/batching bridge that used to
# live in src/backends/software/utils/.  Consolidation (2026-09-20):
# `blend2d/` is the one entry point to the library bridge — no second
# integration point in `utils/`.
# ==============================================================================

# Blend2d paint utilities are an OBJECT lib so both backend_software and
# backend_text can link it without a cycle.  Its sources live under
# src/backends/software/blend2d/ now (moved from utils/ on 2026-09-20).
if(CHRONON3D_USE_BLEND2D)
    add_library(chronon3d_blend2d_paint OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/blend2d_paint.cpp
    )
    target_link_libraries(chronon3d_blend2d_paint PUBLIC chronon3d blend2d::blend2d)
    target_include_directories(chronon3d_blend2d_paint PRIVATE ${CMAKE_SOURCE_DIR})
endif()

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
