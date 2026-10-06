#include <chronon3d/runtime/job/temporal_sample_workspace.hpp>

#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/runtime/job/job_session.hpp>

namespace chronon3d::runtime {

TemporalSampleWorkspace::TemporalSampleWorkspace() = default;
TemporalSampleWorkspace::~TemporalSampleWorkspace() = default;

void TemporalSampleWorkspace::ensure(
    std::size_t sample_count, std::size_t framebuffer_budget_bytes) {
    while (value_caches_.size() < sample_count) {
        value_caches_.push_back(std::make_unique<cache::NodeCache>());
        topology_caches_.push_back(
            std::make_unique<graph::CompiledGraphCache>());
        sessions_.push_back(JobSession::make_temporal_sample_session());
        scratches_.push_back(std::make_unique<TransformScratchBuffer>());
        framebuffer_pools_.push_back(
            cache::FramebufferPool::create_shared(framebuffer_budget_bytes));
        framebuffer_pool_budgets_.push_back(framebuffer_budget_bytes);
    }
    for (std::size_t i = 0; i < sample_count; ++i) {
        if (framebuffer_pool_budgets_[i] == framebuffer_budget_bytes) continue;
        framebuffer_pools_[i] =
            cache::FramebufferPool::create_shared(framebuffer_budget_bytes);
        framebuffer_pool_budgets_[i] = framebuffer_budget_bytes;
    }
}

void TemporalSampleWorkspace::begin_frame() {
    for (std::size_t i = 0; i < value_caches_.size(); ++i) {
        value_caches_[i]->clear();
        sessions_[i]->reset_temporal_history();
    }
}

cache::NodeCache& TemporalSampleWorkspace::value_cache(std::size_t index) {
    return *value_caches_.at(index);
}

graph::CompiledGraphCache& TemporalSampleWorkspace::topology_cache(
    std::size_t index) {
    return *topology_caches_.at(index);
}

JobSession& TemporalSampleWorkspace::session(std::size_t index) {
    return *sessions_.at(index);
}

TransformScratchBuffer& TemporalSampleWorkspace::transform_scratch(
    std::size_t index) {
    return *scratches_.at(index);
}

const std::shared_ptr<cache::FramebufferPool>&
TemporalSampleWorkspace::framebuffer_pool(std::size_t index) const {
    return framebuffer_pools_.at(index);
}

} // namespace chronon3d::runtime
