// Vulkan transform operation wrappers.
#include "vulkan_backend_impl.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace chronon3d::backends::vulkan {
namespace {

template <typename HandleT>
std::uint64_t t05_handle_value(HandleT handle) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

} // namespace

void VulkanBackend::Impl::transform(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    int offset_x, int offset_y, float opacity) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (dst_image.image == src_image.image) {
        throw std::invalid_argument(
            "Vulkan transform destination aliases its source image: destination=" +
            std::to_string(destination) + " source=" + std::to_string(source));
    }
    if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan transform references incompatible surfaces: dst=" +
            std::to_string(dst_image.width) + "x" +
            std::to_string(dst_image.height) + " src=" +
            std::to_string(src_image.width) + "x" +
            std::to_string(src_image.height) +
            (src_image.initialized ? "" : " src-uninitialized"));
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        if (std::getenv("CHRONON3D_T05_DESCRIPTOR_DIAG")) {
            spdlog::info(
                "[t05-descriptor] op=transform pass={} set=0x{:x} "
                "src_image=0x{:x} src_view=0x{:x} src_alloc=0x{:x} "
                "src_extent={}x{} src_init={} dst_image=0x{:x} "
                "dst_view=0x{:x} dst_alloc=0x{:x} dst_extent={}x{} dst_init={}",
                frame_batch.pass_count, t05_handle_value(descriptors),
                t05_handle_value(src_image.image), t05_handle_value(src_image.view),
                t05_handle_value(src_image.allocation), src_image.width,
                src_image.height, src_image.initialized,
                t05_handle_value(dst_image.image), t05_handle_value(dst_image.view),
                t05_handle_value(dst_image.allocation), dst_image.width,
                dst_image.height, dst_image.initialized);
        }
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::Transform);
        record_transform(cmd, descriptors, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan transform requires an active frame batch");
}

void VulkanBackend::Impl::transform_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform_value) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (dst_image.image == src_image.image) {
        throw std::invalid_argument(
            "Vulkan affine transform destination aliases its source image: destination=" +
            std::to_string(destination) + " source=" + std::to_string(source));
    }
    if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan affine transform references incompatible surfaces");
    }
    if (std::getenv("CHRONON3D_T05_SURFACE_ID_DIAG")) {
        spdlog::info(
            "[t05-surface-identity] op=affine-transform src={} src_image=0x{:x} "
            "dst={} dst_image=0x{:x} src_init={} dst_init={} pass={} plan={}",
            source, t05_handle_value(src_image.image), destination,
            t05_handle_value(dst_image.image), src_image.initialized,
            dst_image.initialized, frame_batch.pass_count,
            frame_batch.command_plan != nullptr);
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        if (std::getenv("CHRONON3D_T05_DESCRIPTOR_DIAG")) {
            spdlog::info(
                "[t05-descriptor] op=affine-transform pass={} set=0x{:x} "
                "src_image=0x{:x} src_view=0x{:x} src_alloc=0x{:x} "
                "src_extent={}x{} src_init={} dst_image=0x{:x} "
                "dst_view=0x{:x} dst_alloc=0x{:x} dst_extent={}x{} dst_init={}",
                frame_batch.pass_count, t05_handle_value(descriptors),
                t05_handle_value(src_image.image), t05_handle_value(src_image.view),
                t05_handle_value(src_image.allocation), src_image.width,
                src_image.height, src_image.initialized,
                t05_handle_value(dst_image.image), t05_handle_value(dst_image.view),
                t05_handle_value(dst_image.allocation), dst_image.width,
                dst_image.height, dst_image.initialized);
        }
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::AffineTransform);
        record_transform_affine(cmd, descriptors,
                                dst_image, src_image, transform_value);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error(
        "Vulkan affine transform requires an active frame batch");
}
} // namespace chronon3d::backends::vulkan
