#pragma once

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <memory>
#include <optional>

namespace chronon3d::graph {

/**
 * CompiledGraphCache — RAII owner of the cached compiled render graph.
 *
 * When graph_structure_unchanged is true, the previously built, optimized
 * and compiled graph is reused — skipping build_graph(), optimize_graph()
 * and compile().
 */
class CompiledGraphCache {
public:
    CompiledGraphCache() = default;

    CompiledGraphCache(const CompiledGraphCache&) = delete;
    CompiledGraphCache& operator=(const CompiledGraphCache&) = delete;
    CompiledGraphCache(CompiledGraphCache&&) noexcept = default;
    CompiledGraphCache& operator=(CompiledGraphCache&&) noexcept = default;

    /// Try to take the cached graph if dimensions match.
    /// On success, the internal cache is cleared (single-use).
    [[nodiscard]] std::optional<CompiledFrameGraph> try_take(int width, int height);

    /// Store a new compiled graph for next-frame reuse.
    void store(CompiledFrameGraph&& compiled, int width, int height);

    /// Check whether a cached graph exists for the given dimensions.
    [[nodiscard]] bool has(int width, int height) const noexcept;

    /// Drop the cached graph.
    void reset();

private:
    std::unique_ptr<CompiledFrameGraph> m_cached;
    int m_cached_width{0};
    int m_cached_height{0};
};

} // namespace chronon3d::graph
