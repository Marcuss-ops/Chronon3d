#pragma once

#include <chronon3d/render_graph/render_backend_types.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d {
struct RenderCounters;
namespace renderer { class ProcessorRegistrySnapshot; }
}
namespace chronon3d::cache { class FramebufferPool; }

namespace chronon3d::graph {

/// Diagnostics and capability queries shared by render backends.
class RenderBackendStatsApi {
public:
    virtual ~RenderBackendStatsApi() = default;

    [[nodiscard]] virtual std::string lane_name() const;
    [[nodiscard]] virtual RenderCapabilities capabilities() const noexcept;
    virtual RenderCounters* counters();
    virtual std::shared_ptr<cache::FramebufferPool> framebuffer_pool();
    virtual void export_gpu_telemetry_counters(
        std::vector<std::pair<std::string, std::uint64_t>>& out) const;
    [[nodiscard]] virtual bool supports_native_video_surface() const noexcept;
    [[nodiscard]] virtual bool supports_native_surfaces() const noexcept;
    [[nodiscard]] virtual bool is_batching_supported() const noexcept;
    [[nodiscard]] virtual std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
        processor_snapshot() const noexcept;
    [[nodiscard]] virtual bool requires_processor_snapshot() const noexcept;
};

} // namespace chronon3d::graph
