#pragma once

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace chronon3d {
class TransformScratchBuffer;
namespace runtime { class JobSession; }
}

namespace chronon3d::runtime {

/// Reusable per-frame-slot resources for temporal sample rendering.
/// A JobSession owns one workspace per concurrently leased frame slot, keeping
/// sample state isolated while retaining its allocations across slot reuse.
class TemporalSampleWorkspace final {
public:
    TemporalSampleWorkspace();
    ~TemporalSampleWorkspace();

    TemporalSampleWorkspace(const TemporalSampleWorkspace&) = delete;
    TemporalSampleWorkspace& operator=(const TemporalSampleWorkspace&) = delete;

    void ensure(std::size_t sample_count, std::size_t framebuffer_budget_bytes);
    void begin_frame();

    [[nodiscard]] cache::NodeCache& value_cache(std::size_t index);
    [[nodiscard]] graph::CompiledGraphCache& topology_cache(std::size_t index);
    [[nodiscard]] JobSession& session(std::size_t index);
    [[nodiscard]] TransformScratchBuffer& transform_scratch(std::size_t index);
    [[nodiscard]] const std::shared_ptr<cache::FramebufferPool>&
    framebuffer_pool(std::size_t index) const;

private:
    std::vector<std::unique_ptr<cache::NodeCache>> value_caches_;
    std::vector<std::unique_ptr<graph::CompiledGraphCache>> topology_caches_;
    std::vector<std::unique_ptr<JobSession>> sessions_;
    std::vector<std::unique_ptr<TransformScratchBuffer>> scratches_;
    std::vector<std::shared_ptr<cache::FramebufferPool>> framebuffer_pools_;
    std::vector<std::size_t> framebuffer_pool_budgets_;
};

} // namespace chronon3d::runtime
