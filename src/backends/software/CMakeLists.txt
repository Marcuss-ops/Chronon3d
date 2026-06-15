# ==============================================================================
# src/backends/software/CMakeLists.txt — chronon3d_backend_software
# ==============================================================================

add_library(chronon3d_backend_software STATIC
    # Top-level files only — subsystem sources are in per-directory CMakeLists.txt
    software_compositor.cpp
    software_effect_runner.cpp
    software_node_dispatcher.cpp
    software_renderer.cpp
    buffer_ring.cpp
    scratch_buffer.cpp
    graph_cache.cpp
)
set_target_properties(chronon3d_backend_software PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_backend_software
    PUBLIC
        chronon3d
        chronon3d_graph
        chronon3d_cache
        chronon3d_effects
        chronon3d_backend_assets
        chronon3d_backend_image
    PRIVATE
        spdlog::spdlog_header_only
        hwy::hwy
        fmt::fmt
        xxHash::xxhash
        tomlplusplus::tomlplusplus
        TBB::tbb
)
if(CHRONON3D_USE_BLEND2D)
    target_link_libraries(chronon3d_backend_software PUBLIC blend2d::blend2d)
endif()
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_backend_software PUBLIC chronon3d_backend_text)
endif()
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
target_include_directories(chronon3d_backend_software PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_backend_software PRIVATE
    <chronon3d/detail/pch.hpp>
    $<$<BOOL:${CHRONON3D_USE_BLEND2D}>:<blend2d.h>>
)

# ==============================================================================
# Subsystem source files (each directory adds its sources via target_sources)
# ==============================================================================
add_subdirectory(processors)
add_subdirectory(rasterizers)
add_subdirectory(sampling)
add_subdirectory(utils)
add_subdirectory(diagnostics)
add_subdirectory(simd)
add_subdirectory(kernels)
add_subdirectory(specialized)
