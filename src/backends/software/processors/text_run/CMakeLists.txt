# ==============================================================================
# src/backends/software/processors/text_run/CMakeLists.txt
# ==============================================================================

if(CHRONON3D_ENABLE_TEXT)
    target_sources(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_matrix.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor.cpp
        # M1.5#6 — four-stage split of the text_run_processor hot path.
        # Orchestrator (text_run_processor.cpp) calls these via the
        # internal `text_run_stages.hpp` contract; they live strictly
        # under src/backends/software/processors/text_run/ for AGENTS.md
        # v0.1 Cat-3 freeze compliance (NOT promoted to include/).
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor/prepare.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor/raster.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor/effects.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor/composite.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor/scratch.cpp
    )
    # `bucket_radius_for_tier` (the 4-tier blur-radius mapping) lives
    # in the PUBLIC header <chronon3d/backends/software/text_run_processor.hpp>
    # so tests can call the same function the production code uses.
    # No separate CMakeLists entry is needed for header-only inlines.
endif()
