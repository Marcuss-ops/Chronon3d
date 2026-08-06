#pragma once

// ---------------------------------------------------------------------------
// runtime/cache_domains.hpp
//
// Internal, non-owning boundaries for the two runtime-owned cache domains:
//   CompiledTopologyCache -> existing CompiledGraphCache storage
//   FrameValueCache       -> existing NodeCache storage
//
// These facades never own or duplicate cache storage. They are ephemeral:
// callers must not retain them beyond the lifetime of the referenced owner.
// FrameHistoryState is declared separately in history_state.hpp because its
// owner is RenderSession rather than RenderRuntime.
// ---------------------------------------------------------------------------

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>

namespace chronon3d::runtime {

class CompiledTopologyCache final {
public:
    explicit CompiledTopologyCache(graph::CompiledGraphCache& cache) noexcept
        : m_cache(&cache) {}

    CompiledTopologyCache(const CompiledTopologyCache&) = delete;
    CompiledTopologyCache& operator=(const CompiledTopologyCache&) = delete;

    /// Drop only compiled topology. Frame values and temporal history survive.
    void reset() { m_cache->reset(); }

    [[nodiscard]] graph::CompiledGraphCache& storage() noexcept { return *m_cache; }
    [[nodiscard]] const graph::CompiledGraphCache& storage() const noexcept { return *m_cache; }

private:
    graph::CompiledGraphCache* m_cache;
};

class FrameValueCache final {
public:
    explicit FrameValueCache(cache::NodeCache& cache) noexcept
        : m_cache(&cache) {}

    FrameValueCache(const FrameValueCache&) = delete;
    FrameValueCache& operator=(const FrameValueCache&) = delete;

    /// Drop only evaluated node values. Topology and temporal history survive.
    void reset() { m_cache->clear(); }

    [[nodiscard]] cache::NodeCache& storage() noexcept { return *m_cache; }
    [[nodiscard]] const cache::NodeCache& storage() const noexcept { return *m_cache; }

private:
    cache::NodeCache* m_cache;
};

} // namespace chronon3d::runtime
