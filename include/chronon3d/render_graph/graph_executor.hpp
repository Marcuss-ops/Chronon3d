#pragma once

#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/render_graph/render_graph.hpp>

namespace chronon3d::render_graph {

class GraphExecutor {
public:
    [[nodiscard]] cache::FrameCache& frame_cache() { return m_frame_cache; }
    [[nodiscard]] const cache::FrameCache& frame_cache() const { return m_frame_cache; }

    [[nodiscard]] std::unique_ptr<Framebuffer> execute(const RenderGraph& graph,
                                                       RenderGraphExecutionContext& ctx,
                                                       std::string composition_id = {},
                                                       u64 scene_hash = 0,
                                                       u64 render_hash = 0);

private:
    cache::FrameCache m_frame_cache;
};

} // namespace chronon3d::render_graph
