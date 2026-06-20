# ==============================================================================
# src/backends/software/processors/text_run/CMakeLists.txt
# ==============================================================================

if(CHRONON3D_ENABLE_TEXT)
    target_sources(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_matrix.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text_run_processor.cpp
    )
    # `bucket_radius_for_tier` (the 4-tier blur-radius mapping) lives
    # in the PUBLIC header <chronon3d/backends/software/text_run_processor.hpp>
    # so tests can call the same function the production code uses.
    # No separate CMakeLists entry is needed for header-only inlines.
endif()
