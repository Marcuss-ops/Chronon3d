#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace chronon3d::backends::vulkan {

/// Stable identifiers for Vulkan kernels.  Render operations resolve a
/// pipeline through this registry instead of embedding shader/pipeline
/// selection in each operation.
enum class GpuKernelId : std::uint8_t {
    Composite,
    Transform,
    AffineTransform,
    Blur,
    ColorAdjust,
    Matte,
    TextRun,
    FillRect,
    LayerBatch,
    TextBatch,
    TextTileBin,
    TextTileRaster,
};

class GpuKernelRegistry {
public:
    using PipelineHandle = std::uintptr_t;

    bool register_kernel(GpuKernelId id, PipelineHandle pipeline) noexcept {
        if (pipeline == 0) return false;
        return m_pipelines.emplace(id, pipeline).second;
    }

    [[nodiscard]] PipelineHandle resolve(GpuKernelId id) const noexcept {
        const auto it = m_pipelines.find(id);
        return it == m_pipelines.end() ? 0 : it->second;
    }

    [[nodiscard]] bool contains(GpuKernelId id) const noexcept {
        return resolve(id) != 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_pipelines.size(); }

    /// Enumerate the kernels actually registered in this registry.
    ///
    /// Lifecycle authorities use this instead of maintaining a second manual
    /// list of every GpuKernelId.  The registry remains the source of truth for
    /// both lookup and destruction.
    template <typename Fn>
    void for_each_registered(Fn&& fn) const {
        for (const auto& [id, pipeline] : m_pipelines) {
            fn(id, pipeline);
        }
    }

private:
    std::unordered_map<GpuKernelId, PipelineHandle> m_pipelines;
};

} // namespace chronon3d::backends::vulkan
