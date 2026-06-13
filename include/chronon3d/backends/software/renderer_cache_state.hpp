#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/backends/software/graph_cache.hpp>

#include <memory>

namespace chronon3d {

/**
 * RendererCacheState — caches and pools that survive across frames.
 *
 * Kept separate from per-frame state so that cache invalidation and
 * warm-up are explicit and easy to reason about.
 */
struct RendererCacheState {
    mutable cache::NodeCache              node_cache;
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool;
    CompiledGraphCache                    graph_cache;
};

} // namespace chronon3d
