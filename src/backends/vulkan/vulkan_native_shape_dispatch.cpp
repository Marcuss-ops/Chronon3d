#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include "vulkan_native_shape_dispatch_detail.hpp"
#include <chronon3d/cache/framebuffer_copy.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include "../../render_graph/nodes/detail/native_promotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

namespace {

template <typename Result>
void throw_if_transfer_failed(const Result& result, std::string_view operation) {
    if (!result.ok())
        throw std::runtime_error(
            "Vulkan CPU fallback " + std::string(operation) + " failed: " +
            result.error().message);
}







void sync_gpu_to_cpu(VulkanBackend& backend, Framebuffer& fb) {
    const auto handle = fb.surface_handle();
    if (handle == runtime::kInvalidRenderSurfaceHandle || !fb.is_gpu_authoritative()) {
        return;
    }
    const auto w = fb.width();
    const auto h = fb.height();
    if (w <= 0 || h <= 0) return;
    if (!fb.ensure_cpu_storage()) {
        throw std::runtime_error("Vulkan CPU fallback readback has no framebuffer storage");
    }

    if (fb.stride() == w) {
        std::span<float> dst(reinterpret_cast<float*>(fb.data()), static_cast<std::size_t>(w) * h * 4);
        const auto result = backend.download_surface(handle, dst);
        throw_if_transfer_failed(result, "readback");
    } else {
        std::vector<float> packed(static_cast<std::size_t>(w) * h * 4);
        const auto result = backend.download_surface(handle, packed);
        throw_if_transfer_failed(result, "readback");
        FramebufferCopyAuthority::copy_rows(
            fb.pixels_row(0), fb.stride(),
            reinterpret_cast<const Color*>(packed.data()), w, w, h);
    }
    fb.mark_cpu_gpu_synchronized();
}

void sync_cpu_to_gpu(VulkanBackend& backend, Framebuffer& fb) {
    const auto handle = fb.surface_handle();
    if (handle == runtime::kInvalidRenderSurfaceHandle) {
        return;
    }
    const auto w = fb.width();
    const auto h = fb.height();
    if (w <= 0 || h <= 0) return;

    const auto desc = runtime::SurfaceDesc::make(
        static_cast<std::uint32_t>(w),
        static_cast<std::uint32_t>(h),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient);

    if (fb.stride() == w) {
        std::span<const float> src(reinterpret_cast<const float*>(fb.data()), static_cast<std::size_t>(w) * h * 4);
        const auto result = backend.upload_surface(handle, desc, src);
        throw_if_transfer_failed(result, "upload");
    } else {
        std::vector<float> packed(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t y = 0; y < static_cast<std::size_t>(h); ++y) {
            const Color* row = fb.data() + y * static_cast<std::size_t>(fb.stride());
            std::memcpy(packed.data() + y * static_cast<std::size_t>(w) * 4, row, static_cast<std::size_t>(w) * sizeof(Color));
        }
        const auto result = backend.upload_surface(handle, desc, packed);
        throw_if_transfer_failed(result, "upload");
    }
    fb.mark_gpu_authoritative();
}

} // namespace

void VulkanBackend::unsupported(const char* operation) {
    throw std::runtime_error(std::string{"VulkanBackend::"} + operation +
                             ": RenderSurface execution is not wired yet");
}

void VulkanBackend::apply_per_pixel_dof(
    Framebuffer& framebuffer, std::span<const float> depth,
    const DepthOfFieldSettings& dof, const LensModel& lens,
    const std::optional<raster::BBox>& clip) {
    // A CPU depth vector cannot be consumed by a device-local image kernel.
    // Never hide that contract violation behind an implicit GPU→CPU readback.
    (void)framebuffer; (void)depth; (void)dof; (void)lens; (void)clip;
    unsupported("apply_per_pixel_dof (native depth surface required)");
}

graph::RenderOpResult VulkanBackend::draw_node(Framebuffer& framebuffer, const RenderNode& node,
                                               const RenderState& state, const Camera& camera,
                                               int width, int height) {
    const bool path_diag = std::getenv("CHRONON3D_PATH_DIAG") != nullptr;
    if (path_diag && node.shape.type() == ShapeType::Path)
        spdlog::info("[native-path-dispatch] node={} handle={} backend_valid={} cpu_data={} fill={} stroke={} brush={} projected={}",
            node.name, framebuffer.surface_handle(),
            framebuffer.surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
                is_native_surface_valid(framebuffer.surface_handle()),
            framebuffer.data() != nullptr, node.shape.path().fill.enabled,
            node.shape.path().stroke.enabled,
            node.shape.path().stroke.brush.has_value(),
            graph::native_promotion::is_projected(state));
    const auto completed_native_draw = [&framebuffer] {
        framebuffer.mark_gpu_authoritative();
        return graph::RenderOpResult(graph::RenderOpOutcome{1});
    };
    if (native_shape_dispatch_detail::try_native_grid(*this, framebuffer, node, state)) return completed_native_draw();
    if (native_shape_dispatch_detail::try_native_path_fill(*this, framebuffer, node, state)) return completed_native_draw();
    if (native_shape_dispatch_detail::try_native_path_stroke(*this, framebuffer, node, state)) return completed_native_draw();
    if (native_shape_dispatch_detail::try_native_solid_rect(*this, framebuffer, node, state)) return completed_native_draw();

    if (path_diag && node.shape.type() == ShapeType::Path)
        spdlog::warn("[native-path-dispatch] native path routes rejected node={}", node.name);

    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        auto result = m_fallback_backend->draw_node(framebuffer, node, state, camera, width, height);
        if (!result.ok()) return result;
        sync_cpu_to_gpu(*this, framebuffer);
        return result;
    }

    (void)camera; (void)width; (void)height;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_node: no legacy-node fallback attached; node='" +
        std::string(node.name) + "' shape_type=" +
        std::to_string(static_cast<int>(node.shape.type()))));
}

void VulkanBackend::apply_effect_stack(
    Framebuffer& framebuffer, const EffectStack& stack,
    const effects::EffectExecutionContext& context) {
    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        m_fallback_backend->apply_effect_stack(framebuffer, stack, context);
        sync_cpu_to_gpu(*this, framebuffer);
        return;
    }
    (void)framebuffer; (void)stack; (void)context;
    unsupported("apply_effect_stack");
}

void VulkanBackend::composite_layer(Framebuffer& destination, const Framebuffer& source,
                                    BlendMode mode, const std::optional<raster::BBox>& clip,
                                    CompositeOperator op) {
    if (destination.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        source.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        mode != BlendMode::Normal || op != CompositeOperator::SourceOver ||
        destination.width() != source.width() || destination.height() != source.height()) {
        if (m_fallback_backend) {
            sync_gpu_to_cpu(*this, destination);
            sync_gpu_to_cpu(*this, const_cast<Framebuffer&>(source));
            m_fallback_backend->composite_layer(destination, source, mode, clip, op);
            sync_cpu_to_gpu(*this, destination);
            return;
        }
        if (destination.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
            source.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
            throw std::invalid_argument(
                "VulkanBackend::composite_layer: missing native surface handle "
                "(CPU blit fallback was demolished; materialize surfaces first)");
        }
        if (mode != BlendMode::Normal || op != CompositeOperator::SourceOver) {
            throw std::runtime_error("VulkanBackend::composite_layer: only Normal/SourceOver is implemented");
        }
        throw std::runtime_error("VulkanBackend::composite_layer: surface dimensions differ");
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    composite_legacy_surface(destination, source, mode, clip);
    destination.mark_gpu_authoritative();
#else
    unsupported("composite_layer");
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run(
    Framebuffer& framebuffer, const TextRunShape& shape,
    const glm::mat4& model_matrix, float opacity) {
    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        auto result = m_fallback_backend->draw_text_run(framebuffer, shape, model_matrix, opacity);
        if (!result.ok()) return result;
        sync_cpu_to_gpu(*this, framebuffer);
        return result;
    }
    (void)framebuffer; (void)shape; (void)model_matrix; (void)opacity;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run: no legacy-node fallback attached"));
}

void VulkanBackend::apply_blur(
    Framebuffer& framebuffer, float radius,
    const std::optional<raster::BBox>& clip) {
    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        m_fallback_backend->apply_blur(framebuffer, radius, clip);
        sync_cpu_to_gpu(*this, framebuffer);
        return;
    }
    (void)framebuffer; (void)radius; (void)clip;
    unsupported("apply_blur");
}

} // namespace chronon3d::backends::vulkan
