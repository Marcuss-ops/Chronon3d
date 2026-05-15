#include <chronon3d/runtime/pipeline.hpp>
#include <taskflow/taskflow.hpp>
#include <chronon3d/core/profiling.hpp>

namespace chronon3d {

void RenderPipeline::run(Frame start, Frame end, std::function<void(RenderedFrame)> output_callback) {
    ZoneScoped;
    const i32 count = static_cast<i32>(end - start);
    if (count <= 0) return;

    std::vector<EvaluatedFrame> eval_frames;
    eval_frames.reserve(static_cast<size_t>(count));
    for (Frame f = start; f < end; ++f) {
        ZoneScopedN("EvaluateFrame");
        auto arena = std::make_shared<FrameArena>();
        auto scene = m_composition->evaluate(f, arena->resource());
        eval_frames.push_back({f, std::move(arena), std::move(scene)});
    }

    std::vector<RenderedFrame> frame_buffer(static_cast<size_t>(count));
    {
        tf::Executor executor;
        tf::Taskflow taskflow;
        for (i32 i = 0; i < count; ++i) {
            const size_t idx = static_cast<size_t>(i);
            taskflow.emplace([&, idx]() {
                ZoneScopedN("RenderFrame");
                auto& ef = eval_frames[idx];
                frame_buffer[idx] = RenderedFrame{
                    ef.frame,
                    m_renderer->render_scene(ef.scene, m_composition->camera,
                                             m_composition->width(), m_composition->height()),
                    ef.arena
                };
            });
        }
        executor.run(taskflow).wait();
    }

    for (auto& rf : frame_buffer) {
        ZoneScopedN("OutputFrame");
        output_callback(std::move(rf));
    }
}

} // namespace chronon3d
