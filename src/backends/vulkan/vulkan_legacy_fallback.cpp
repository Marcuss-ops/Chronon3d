#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace chronon3d::backends::vulkan {

void VulkanBackend::unsupported(const char* operation) {
    throw std::runtime_error(std::string{"VulkanBackend::"} + operation +
                             ": RenderSurface execution is not wired yet");
}

void VulkanBackend::record_software_fallback(
    const char* reason,
    std::chrono::steady_clock::time_point started) noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    record_fallback_counter(reason, static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, elapsed)));
#else
    (void)reason;
    (void)started;
#endif
}

void VulkanBackend::apply_per_pixel_dof(
    Framebuffer& framebuffer, std::span<const float> depth,
    const DepthOfFieldSettings& dof, const LensModel& lens,
    const std::optional<raster::BBox>& clip) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->apply_per_pixel_dof(
            framebuffer, depth, dof, lens, clip);
        record_software_fallback("dof", started);
        return;
    }
    unsupported("apply_per_pixel_dof");
}

void VulkanBackend::set_draw_node_fallback(
    std::unique_ptr<graph::RenderBackend> fallback) {
    m_draw_node_fallback = std::move(fallback);
}

void VulkanBackend::draw_node(Framebuffer& framebuffer, const RenderNode& node,
                              const RenderState& state, const Camera& camera,
                              int width, int height) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->draw_node(framebuffer, node, state, camera, width, height);
        record_fallback_shape(node.shape.type() == ShapeType::Image);
        record_software_fallback("draw_node", started);
        return;
    }
    throw std::runtime_error(
        "VulkanBackend::draw_node: no legacy-node fallback attached; node='" +
        std::string(node.name) + "' shape_type=" +
        std::to_string(static_cast<int>(node.shape.type())));
}

void VulkanBackend::apply_effect_stack(
    Framebuffer& framebuffer, const EffectStack& stack,
    const effects::EffectExecutionContext& context) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->apply_effect_stack(framebuffer, stack, context);
        record_software_fallback("effect", started);
        return;
    }
    unsupported("apply_effect_stack");
}

void VulkanBackend::composite_layer(Framebuffer& destination, const Framebuffer& source,
                                    BlendMode mode, const std::optional<raster::BBox>& clip,
                                    CompositeOperator op) {
    if (mode != BlendMode::Normal || op != CompositeOperator::SourceOver) {
        if (m_draw_node_fallback) {
            const auto started = std::chrono::steady_clock::now();
            m_draw_node_fallback->composite_layer(destination, source, mode, clip, op);
            record_fallback_composite(false);
            record_software_fallback("composite", started);
            return;
        }
        throw std::runtime_error("VulkanBackend::composite_layer: only Normal/SourceOver is implemented");
    }
    if (destination.width() != source.width() || destination.height() != source.height()) {
        if (m_draw_node_fallback) {
            const auto started = std::chrono::steady_clock::now();
            m_draw_node_fallback->composite_layer(destination, source, mode, clip, op);
            record_fallback_composite(true);
            record_software_fallback("composite", started);
            return;
        }
        throw std::runtime_error("VulkanBackend::composite_layer: surface dimensions differ");
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    composite_legacy_surface(destination, source, mode, clip);
#else
    unsupported("composite_layer");
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run(
    Framebuffer& framebuffer, const TextRunShape& shape,
    const glm::mat4& model_matrix, float opacity) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        auto result = m_draw_node_fallback->draw_text_run(
            framebuffer, shape, model_matrix, opacity);
        record_software_fallback("text_run", started);
        return result;
    }
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run: no legacy-node fallback attached"});
}

void VulkanBackend::apply_blur(
    Framebuffer& framebuffer, float radius,
    const std::optional<raster::BBox>& clip) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->apply_blur(framebuffer, radius, clip);
        record_software_fallback("blur", started);
        return;
    }
    unsupported("apply_blur");
}

} // namespace chronon3d::backends::vulkan
