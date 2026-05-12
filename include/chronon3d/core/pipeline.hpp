#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/core/arena.hpp>
#include <taskflow/taskflow.hpp>
#include <chronon3d/core/profiling.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace chronon3d {

struct EvaluatedFrame {
    Frame frame;
    Scene scene;
    std::shared_ptr<FrameArena> arena;
};

struct RenderedFrame {
    Frame frame;
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<FrameArena> arena;
};

class RenderPipeline {
public:
    RenderPipeline(std::shared_ptr<const Composition> composition,
                   std::shared_ptr<Renderer> renderer)
        : m_composition(std::move(composition))
        , m_renderer(std::move(renderer)) {}

    void run(Frame start, Frame end, std::function<void(RenderedFrame)> output_callback) {
        ZoneScoped;
        const i32 count = static_cast<i32>(end - start);
        if (count <= 0) return;

        // Stage 1: evaluate all frames in order.
        // Composition lambdas are not thread-safe; sequential evaluation is required.
        std::vector<EvaluatedFrame> eval_frames;
        eval_frames.reserve(static_cast<size_t>(count));
        for (Frame f = start; f < end; ++f) {
            ZoneScopedN("EvaluateFrame");
            auto arena = std::make_shared<FrameArena>();
            eval_frames.push_back({f, m_composition->evaluate(f, arena->resource()), std::move(arena)});
        }

        // Stage 2: render all frames in parallel into a pre-indexed vector.
        // render_scene() creates a fresh Framebuffer per call and has no mutable state,
        // so concurrent calls on different indices are safe.
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

        // Stage 3: emit frames in original order — guaranteed deterministic.
        for (auto& rf : frame_buffer) {
            ZoneScopedN("OutputFrame");
            output_callback(std::move(rf));
        }
    }

private:
    std::shared_ptr<const Composition> m_composition;
    std::shared_ptr<Renderer> m_renderer;
};

} // namespace chronon3d
