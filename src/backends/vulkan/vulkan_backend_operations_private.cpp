// vulkan_backend_operations_private.cpp — Vulkan composite operation.
//
// Filter/warp ops live in vulkan_backend_operations_filter_private.cpp,
// glow/color/matte ops in vulkan_backend_operations_color_private.cpp,
// text batching in vulkan_backend_text_ops_private.cpp and layer batching
// in vulkan_backend_layer_ops_private.cpp.

#include "vulkan_backend_impl.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace chronon3d::backends::vulkan {
namespace {

std::int32_t composite_blend_mode(BlendMode mode, bool replace,
                                  bool weighted_source_over) {
    if (replace) return 2;
    if (weighted_source_over) return 7;
    switch (mode) {
        case BlendMode::Add: return 1;
        case BlendMode::Multiply: return 3;
        case BlendMode::Screen: return 4;
        case BlendMode::Overlay: return 5;
        case BlendMode::Normal: return 0;
    }
    return 0;
}

} // namespace

void VulkanBackend::Impl::composite(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    BlendMode mode,
                                    const std::optional<raster::BBox>& clip,
                                    bool replace, float source_scale,
                                    bool weighted_source_over) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan composite references incompatible surfaces: dst=" +
            std::to_string(dst_image.width) + "x" +
            std::to_string(dst_image.height) + " src=" +
            std::to_string(src_image.width) + "x" +
            std::to_string(src_image.height) +
            (src_image.initialized ? "" : " src-uninitialized"));
    }
    const auto blend_mode = composite_blend_mode(mode, replace, weighted_source_over);
    const bool premultiplied_source_over =
        !replace && !weighted_source_over && mode == BlendMode::Normal;
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_composite(cmd, descriptors, dst_image, src_image,
                         blend_mode, source_scale, kIdentityTint, clip,
                         premultiplied_source_over);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan composite requires an active frame batch");
}

} // namespace chronon3d::backends::vulkan
