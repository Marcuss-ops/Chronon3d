#include <chronon3d/backends/software/graph_cache.hpp>

namespace chronon3d {

std::optional<graph::CompiledFrameGraph> CompiledGraphCache::try_take(int width, int height) {
    if (!m_cached || m_cached_width != width || m_cached_height != height) {
        return std::nullopt;
    }
    auto result = std::move(*m_cached);
    m_cached.reset();
    return result;
}

void CompiledGraphCache::store(graph::CompiledFrameGraph&& compiled, int width, int height) {
    m_cached = std::make_unique<graph::CompiledFrameGraph>(std::move(compiled));
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

} // namespace chronon3d
