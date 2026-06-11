# ==============================================================================
# src/backends/software/CMakeLists.txt — chronon3d_backend_software
# ==============================================================================

add_library(chronon3d_backend_software STATIC
    processors/builtin_processors.cpp
    processors/path_processor.cpp
    processors/software_effect_processors.cpp
    processors/image/software_image_processor.cpp
    processors/image/software_tiled_image_processor.cpp
    kernels/grid_background_kernel.cpp
    processors/software_grid_background_processor.cpp
    processors/software_mesh_processor.cpp
    processors/software_line_processor.cpp
    processors/software_shape_processor.cpp
    processors/text/software_text_effects.cpp
    processors/text/software_text_processor.cpp
    processors/text/text_glow.cpp
    processors/text/text_shadow.cpp
    processors/software_utility_processors.cpp
    rasterizers/line_rasterizer.cpp
    rasterizers/path/path_cache.cpp
    rasterizers/path/path_rasterizer.cpp
    rasterizers/path/path_stroke.cpp
    rasterizers/path/pip.cpp
    rasterizers/card/projected_card_rasterizer.cpp
    rasterizers/card/card3d_material_rasterizer.cpp
    rasterizers/scanline_rasterizer.cpp
    rasterizers/shape_rasterizer.cpp
    software_compositor.cpp
    software_effect_runner.cpp
    software_node_dispatcher.cpp
    software_renderer.cpp
    buffer_ring.cpp
    scratch_buffer.cpp
    graph_cache.cpp
    diagnostics/bbox_overlay.cpp
    diagnostics/layout_preview_overlay.cpp
    diagnostics/nulls_overlay.cpp
    diagnostics/internal/helpers.cpp
    utils/framebuffer_analysis.cpp
    utils/projection_utils.cpp
    utils/blend2d_bridge_core.cpp
    utils/blend2d_bridge_transforms.cpp
    utils/blend2d_bridge_transforms_fb.cpp
    specialized/fake_box3d_renderer.cpp
    specialized/grid_plane_renderer.cpp
    specialized/mesh_renderer.cpp
    utils/effects/effect_blur.cpp
    utils/effects/effect_color.cpp
    utils/effects/effect_wave.cpp
    utils/effects/glow_pipeline.cpp
    utils/effects/effect_glow_impl.cpp
    utils/effects/effect_shadow_impl.cpp
    utils/effects/effect_bloom_impl.cpp
    utils/effects/effect_stack.cpp
    simd/highway_color_kernels.cpp
    simd/highway_rasterize_kernels.cpp
    simd/highway_dof_kernels.cpp
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
        chronon3d_backend_text
        blend2d::blend2d
    PRIVATE
        spdlog::spdlog_header_only
        hwy::hwy
        fmt::fmt
        xxHash::xxhash
        tomlplusplus::tomlplusplus
        TBB::tbb
)
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
target_include_directories(chronon3d_backend_software PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_backend_software PRIVATE
    <chronon3d/detail/pch.hpp>
    <blend2d.h>
)
