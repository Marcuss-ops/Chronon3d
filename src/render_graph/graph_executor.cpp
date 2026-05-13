#include <chronon3d/render_graph/graph_executor.hpp>

#include <utility>

namespace chronon3d::render_graph {

std::unique_ptr<Framebuffer> GraphExecutor::execute(const RenderGraph& graph,
                                                    RenderGraphExecutionContext& ctx,
                                                    std::string composition_id,
                                                    u64 scene_hash,
                                                    u64 render_hash)
{
    cache::FrameCacheKey key{
        .composition_id = std::move(composition_id),
        .frame = ctx.frame,
        .width = ctx.width,
        .height = ctx.height,
        .scene_hash = scene_hash,
        .render_hash = render_hash,
    };

    if (const auto* cached = m_frame_cache.find(key); cached && *cached && (*cached)->width() == ctx.width && (*cached)->height() == ctx.height) {
        return std::make_unique<Framebuffer>(**cached);
    }

    auto result = graph.execute(ctx);
    if (result) {
        m_frame_cache.store(std::move(key), std::make_shared<Framebuffer>(*result));
    }
    return result;
}

} // namespace chronon3d::render_graph
