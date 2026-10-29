// vulkan_backend_surface_api_ops_private.cpp — composite, fill and transform adapters.
// Split from vulkan_backend_surface_api.cpp per issue #51; each adapter is a
// thin batched wrapper over the Impl ops.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif
#include "vulkan_backend_surface_api_detail.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

graph::RenderOpResult VulkanBackend::composite_surfaces(
    runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
    BlendMode mode, CompositeOperator op,
    const std::optional<raster::BBox>& clip) {
    if ((mode != BlendMode::Normal && mode != BlendMode::Add &&
         mode != BlendMode::Multiply && mode != BlendMode::Screen &&
         mode != BlendMode::Overlay) ||
        op != CompositeOperator::SourceOver) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "VulkanBackend::composite_surfaces: blend mode/operator is unsupported"));
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination, source, mode, clip);
    });
#else
    (void)destination; (void)source;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::composite_surfaces: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::composite_surface_weighted(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float opacity,
    const std::optional<raster::BBox>& clip) {
    if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::InvalidInput,
            "VulkanBackend::composite_surface_weighted: opacity must be finite and in [0, 1]"));
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination, source, BlendMode::Normal, clip,
                          false, opacity, true);
    });
#else
    (void)destination; (void)source; (void)clip;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::composite_surface_weighted: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::accumulate_surface_weighted(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float weight) {
    if (!std::isfinite(weight)) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::InvalidInput,
            "VulkanBackend::accumulate_surface_weighted: weight must be finite"));
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        // Add mode is premultiplied channel addition in composite.comp. Keep
        // the sample weight as a scalar; source-over weighting is not an
        // averaging operation and would darken later shutter samples.
        m_impl->composite(destination, source, BlendMode::Add, std::nullopt,
                          false, weight, false);
    });
#else
    (void)destination; (void)source;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::accumulate_surface_weighted: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::copy_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination, source, BlendMode::Normal, clip, true);
    });
#else
    (void)destination; (void)source; (void)clip;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::copy_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::copy_surface_auxiliary(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination, source, BlendMode::Normal, clip, true,
                          1.0f, false, false);
    });
#else
    (void)destination; (void)source; (void)clip;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::copy_surface_auxiliary: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_rect_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Color& color) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_rect(destination, x0, y0, x1, y1, color);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_rect_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::initialize_transparent_surface(
    runtime::RenderSurfaceHandle destination) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->initialize_transparent_surface(destination);
    });
#else
    (void)destination;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::initialize_transparent_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_solid_shape_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    const Vec4& shape, const Vec4& line, const Color& color,
    const std::array<Vec2, 8>& vertices,
    std::span<const std::uint32_t> mask_words) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_solid_shape(destination, x0, y0, x1, y1,
                                 shape, line, color, vertices, mask_words);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1;
    (void)shape; (void)line; (void)color; (void)vertices; (void)mask_words;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_solid_shape_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_dashed_path_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    float width, const Color& color, std::span<const Vec2> points,
    std::span<const std::uint32_t> run_lengths, bool has_fill,
    std::uint32_t fill_vertex_count, const std::array<Vec2, 8>& fill_vertices,
    bool round_cap) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_dashed_path(destination, x0, y0, x1, y1, width, color,
                                 points, run_lengths, has_fill, fill_vertex_count,
                                 fill_vertices, round_cap);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)width;
    (void)color; (void)points; (void)run_lengths; (void)has_fill;
    (void)fill_vertex_count; (void)fill_vertices; (void)round_cap;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_dashed_path_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_path_surface(
    runtime::RenderSurfaceHandle destination,
    std::span<const Vec2> vertices, const Color& color) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_path(destination, vertices, color);
    });
#else
    (void)destination; (void)vertices; (void)color;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_path_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_grid_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const NativeGridFillParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Host side mirrors render_grid_background_kernel exactly: colors move
    // to linear light and effective opacity scales every alpha (the kernel's
    // degenerate opacity==0 guard is reproduced before the scaling).
    const float effective_opacity =
        (params.opacity > 0.0f) ? params.opacity : 1.0f;
    const Color bg_linear = params.bg_color.to_linear();
    const Color grid_linear = params.grid_color.to_linear();
    const float bg_alpha = bg_linear.a * effective_opacity;
    const float minor_alpha = grid_linear.a * effective_opacity;
    const Color line_color{grid_linear.r, grid_linear.g, grid_linear.b,
                           minor_alpha};
    const Vec4 shape{5.0f, std::max(params.spacing, 1.0f),
                     static_cast<float>(params.major_every),
                     params.style == 1U ? 1.0f : 0.0f};
    const Vec4 line{params.offset.x, params.offset.y,
                    std::max(params.minor_thickness, 0.0f),
                    std::max(params.major_thickness, 0.0f)};
    std::array<Vec2, 8> vertices{};
    vertices[0] = Vec2{bg_linear.r, bg_linear.g};
    vertices[1] = Vec2{bg_linear.b, bg_alpha};
    vertices[2] = Vec2{params.size.x, params.size.y};
    const float edge_fade = std::isfinite(params.edge_fade)
        ? std::max(params.edge_fade, 0.0f) : 0.0f;
    vertices[3] = Vec2{params.centered ? 1.0f : 0.0f, edge_fade};
    vertices[4] = Vec2{std::isfinite(params.major_glow)
        ? std::max(params.major_glow, 0.0f) : 0.0f, 0.0f};
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_grid(destination, x0, y0, x1, y1,
                          line_color, shape, line, vertices);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_grid_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    int offset_x, int offset_y, float opacity) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->transform(destination, source, offset_x, offset_y, opacity);
    });
#else
    (void)destination; (void)source; (void)offset_x; (void)offset_y; (void)opacity;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->transform_affine(destination, source, transform);
    });
#else
    (void)destination; (void)source; (void)transform;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface_affine: Vulkan support is disabled"));
#endif
}
} // namespace chronon3d::backends::vulkan
