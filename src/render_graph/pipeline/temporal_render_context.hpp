#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>

#include <memory>

namespace chronon3d::graph {

/// Internal per-sample domains for temporal accumulation. The main renderer
/// borrows only immutable runtime services; all mutable sample state lives in
/// this object and is never committed to the main frame session.
struct TemporalRenderContext {
    TemporalSampleKey sample_key{};
    SampleTime sample_time{};
    cache::NodeCache* value_cache{nullptr};
    CompiledGraphCache* topology_cache{nullptr};
    RenderSession* session{nullptr};
    TransformScratchBuffer* scratch{nullptr};
    RenderCounters* counters{nullptr};
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool;
};

} // namespace chronon3d::graph
