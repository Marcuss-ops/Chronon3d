#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>

namespace chronon3d::graph {

std::optional<CompiledFrameGraph> CompiledGraphCache::try_take(int width, int height) {
    if (!m_cached || m_cached_width != width || m_cached_height != height) {
        return std::nullopt;
    }
    auto result = std::move(*m_cached);
    m_cached.reset();
    return result;
}

const CompiledFrameGraph* CompiledGraphCache::peek(int width, int height) const noexcept {
    if (!m_cached || m_cached_width != width || m_cached_height != height) {
        return nullptr;
    }
    return m_cached.get();
}

void CompiledGraphCache::store(CompiledFrameGraph&& compiled, int width, int height) {
    m_cached = std::make_unique<CompiledFrameGraph>(std::move(compiled));
    m_cached_width = width;
    m_cached_height = height;
}

bool CompiledGraphCache::has(int width, int height) const noexcept {
    return m_cached != nullptr && m_cached_width == width && m_cached_height == height;
}

void CompiledGraphCache::reset() {
    m_cached.reset();
    m_cached_width = 0;
    m_cached_height = 0;
}

void CompiledGraphCache::reset_static_bakes() noexcept {
    if (!m_cached) return;
    for (auto& bake : m_cached->program.static_bakes) {
        if (!bake.runtime) continue;
        std::lock_guard lock(bake.runtime->mutex);
        bake.runtime->baked_surface.reset();
        bake.runtime->resolved_key_digest = 0;
        bake.runtime->resolved_bbox.reset();
        bake.is_baked = false;
    }
}

} // namespace chronon3d::graph
