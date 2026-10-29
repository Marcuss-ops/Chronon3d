// vulkan_backend_surface_api_effects_private.cpp — blur, warp, glow/color/matte and text adapters.
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

graph::RenderOpResult VulkanBackend::blur_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source, float radius, bool horizontal,
    bool box_kernel) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->blur(destination, source, radius, horizontal, box_kernel);
    });
#else
    (void)destination; (void)source; (void)radius; (void)horizontal; (void)box_kernel;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::blur_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::dof_depth_stamp_surface(
    runtime::RenderSurfaceHandle depth, runtime::RenderSurfaceHandle layer,
    float depth_z, float radius,
    int clip_x0, int clip_y0, int clip_x1, int clip_y1,
    int origin_x, int origin_y, bool reset) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{depth, layer};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->dof_depth_stamp(depth, layer,
            Impl::DofDepthStampPush{depth_z, radius, clip_x0, clip_y0, clip_x1, clip_y1,
                                    origin_x, origin_y, reset ? 1 : 0});
    });
#else
    (void)depth; (void)layer; (void)depth_z; (void)radius;
    (void)clip_x0; (void)clip_y0; (void)clip_x1; (void)clip_y1;
    (void)origin_x; (void)origin_y; (void)reset;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::dof_depth_stamp_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::dof_blur_surface(
    runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle depth, bool vertical,
    int roi_x0, int roi_y0, int roi_x1, int roi_y1,
    float max_radius, bool normalize_opaque) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{destination, source, depth};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->dof_blur(destination, source, depth,
            Impl::DofBlurPush{vertical ? 1 : 0, roi_x0, roi_y0, roi_x1, roi_y1,
                              max_radius, normalize_opaque ? 1 : 0});
    });
#else
    (void)destination; (void)source; (void)depth; (void)vertical;
    (void)roi_x0; (void)roi_y0; (void)roi_x1; (void)roi_y1;
    (void)max_radius; (void)normalize_opaque;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::dof_blur_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::directional_blur_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float dir_x, float dir_y, float length, int tap_count) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->directional_blur(destination, source, dir_x, dir_y, length, tap_count);
    });
#else
    (void)destination; (void)source; (void)dir_x; (void)dir_y; (void)length; (void)tap_count;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::directional_blur_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::radial_blur_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float center_x_px, float center_y_px, float amount, int tap_count) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->radial_blur(destination, source, center_x_px, center_y_px, amount, tap_count);
    });
#else
    (void)destination; (void)source; (void)center_x_px; (void)center_y_px; (void)amount; (void)tap_count;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::radial_blur_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::warp_bulk_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle aux,
    const graph::WarpBulkParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{destination, source, aux};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->warp_bulk(destination, source, aux, params);
    });
#else
    (void)destination; (void)source; (void)aux; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::warp_bulk_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::generate_bulk_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const graph::GenerateBulkParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->generate_bulk(destination, source, params);
    });
#else
    (void)destination; (void)source; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::generate_bulk_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::lut_bulk_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle aux,
    const graph::LutBulkParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{destination, source, aux};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->lut_bulk(destination, source, aux, params);
    });
#else
    (void)destination; (void)source; (void)aux; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::lut_bulk_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::sharpen_combine_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle blurred, float amount, float threshold) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{destination, source, blurred};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->sharpen_combine(destination, source, blurred, amount, threshold);
    });
#else
    (void)destination; (void)source; (void)blurred; (void)amount; (void)threshold;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::sharpen_combine_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::glow_surfaces(    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    float radius, float intensity, const Color& tint) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 4> touched{
        destination, source, scratch_horizontal, scratch_vertical};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->glow(destination, source, scratch_horizontal, scratch_vertical,
                     radius, intensity, tint);
    });
#else
    (void)destination; (void)source; (void)scratch_horizontal;
    (void)scratch_vertical; (void)radius; (void)intensity; (void)tint;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::glow_surfaces: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::bloom_surfaces(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    float threshold, float radius, float intensity) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 4> touched{
        destination, source, scratch_horizontal, scratch_vertical};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->bloom(destination, source, scratch_horizontal, scratch_vertical,
                      threshold, radius, intensity);
    });
#else
    (void)destination; (void)source; (void)scratch_horizontal;
    (void)scratch_vertical; (void)threshold; (void)radius; (void)intensity;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::bloom_surfaces: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::color_adjust_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float brightness, float contrast, const Color& tint, float tint_amount) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->color_adjust(destination, source, brightness, contrast, tint, tint_amount);
    });
#else
    (void)destination; (void)source; (void)brightness; (void)contrast;
    (void)tint; (void)tint_amount;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::color_adjust_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::color_transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const graph::NativeColorTransform& transform) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->color_transform(destination, source, transform);
    });
#else
    (void)destination; (void)source; (void)transform;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::color_transform_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::chromatic_transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const ChromaticAberrationParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->chromatic_transform(destination, source, params);
    });
#else
    (void)destination; (void)source; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::chromatic_transform_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::lighting_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const Color& multiplier) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->lighting(destination, source, multiplier);
    });
#else
    (void)destination; (void)source; (void)multiplier;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::lighting_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::matte_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle target,
    runtime::RenderSurfaceHandle matte,
    bool luma, bool inverted) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{destination, target, matte};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->matte(destination, target, matte, luma, inverted);
    });
#else
    (void)destination; (void)target; (void)matte; (void)luma; (void)inverted;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::matte_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, atlas};
    const auto res = run_batched_surface_op(touched, {}, [&] {
        m_impl->text_run_surface(destination, atlas, glyphs);
    });
    if (!res.ok()) return res;
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
#else
    (void)destination; (void)atlas; (void)glyphs;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run_surface_timed(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, atlas};
    const auto res = run_batched_surface_op(touched, {}, [&] {
        m_impl->text_run_surface(destination, atlas, glyphs, current_frame,
                                 highlight_color, highlight_enabled);
    });
    if (!res.ok()) return res;
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
#else
    (void)destination; (void)atlas; (void)glyphs; (void)current_frame;
    (void)highlight_color; (void)highlight_enabled;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface_timed: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::GlyphStatic> glyphs,
    std::span<const runtime::TextRunDynamic> runs,
    std::span<const runtime::RenderSurfaceHandle> atlas_pages) {
#ifdef CHRONON3D_ENABLE_VULKAN
    // The atlas pages are resolved through resolve_image() by the op, so they
    // are part of the leased set (read_surfaces = the op's read-only set).
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    const auto res = run_batched_surface_op(touched, atlas_pages, [&] {
        m_impl->draw_text_batch(destination, glyphs, runs, atlas_pages);
    });
    if (!res.ok()) return res;
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
#else
    (void)destination; (void)glyphs; (void)runs; (void)atlas_pages;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_batch: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::execute_layer_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::LayerInstance> instances,
    std::span<const runtime::RenderSurfaceHandle> resources,
    std::span<const float> transforms,
    std::span<const float> paints) {
    for (const auto& instance : instances) {
        if (instance.blend != BlendMode::Normal && instance.blend != BlendMode::Add &&
            instance.blend != BlendMode::Multiply && instance.blend != BlendMode::Screen &&
            instance.blend != BlendMode::Overlay) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "VulkanBackend::execute_layer_batch: blend mode is unsupported"));
        }
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    // Every layer source surface is resolved through resolve_image() by the
    // op, so the compiled resource table is part of the leased read set.
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, resources, [&] {
        m_impl->execute_layer_batch(destination, instances, resources,
                                     transforms, paints);
    });
#else
    (void)destination; (void)instances; (void)resources;
    (void)transforms; (void)paints;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::execute_layer_batch: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_inline_mesh_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle texture,
    std::span<const float> projected_lit_vertices,
    std::span<const std::uint32_t> indices,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    const Color& tint,
    std::optional<float> alpha_cutoff) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    const std::array<runtime::RenderSurfaceHandle, 1> reads{texture};
    return run_batched_surface_op(touched, reads, [&] {
        m_impl->draw_inline_mesh(destination, texture, projected_lit_vertices,
                                 indices, x0, y0, x1, y1, tint, alpha_cutoff);
    });
#else
    (void)destination; (void)texture; (void)projected_lit_vertices; (void)indices;
    (void)x0; (void)y0; (void)x1; (void)y1; (void)tint; (void)alpha_cutoff;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_inline_mesh_surface: Vulkan support is disabled"));
#endif
}

// DEMOLISHED (P1.4): VulkanBackend::preallocate_plan_surfaces removed —
// native surface materialization is the lazy VulkanSurfaceAuthority path.

void VulkanBackend::composite_legacy_surface(
    Framebuffer& destination, const Framebuffer& source, BlendMode mode,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{
        destination.surface_handle(), source.surface_handle()};
    const auto result = run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination.surface_handle(), source.surface_handle(), mode, clip);
    });
    if (!result.ok()) {
        throw std::runtime_error(result.error().message);
    }
#else
    (void)destination;
    (void)source;
    (void)mode;
    (void)clip;
    unsupported("composite_layer");
#endif
}

std::unique_ptr<graph::RenderBackend> make_vulkan_backend(
    std::uint32_t device_index) {
    return std::make_unique<VulkanBackend>(device_index);
}
} // namespace chronon3d::backends::vulkan
