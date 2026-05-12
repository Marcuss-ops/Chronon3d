#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/core/arena.hpp>
#include <concurrentqueue.h>
#include <taskflow/taskflow.hpp>
#include <tracy/Tracy.hpp>
#include <memory>
#include <functional>

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
        tf::Executor executor;
        tf::Taskflow taskflow;

        moodycamel::ConcurrentQueue<EvaluatedFrame> eval_queue;
        moodycamel::ConcurrentQueue<RenderedFrame> render_queue;

        std::atomic<bool> eval_done{false};
        std::atomic<bool> render_done{false};

        // Stage 1: Evaluation
        auto eval_task = taskflow.emplace([&]() {
            ZoneScopedN("EvaluationStage");
            for (i64 f = start; f < end; ++f) {
                ZoneScopedN("EvaluateFrame");
                auto arena = std::make_shared<FrameArena>();
                Scene scene = m_composition->evaluate(Frame{f}, arena->resource());
                eval_queue.enqueue({Frame{f}, std::move(scene), std::move(arena)});
            }
            eval_done = true;
        }).name("Evaluation");

        // Stage 2: Rendering
        auto render_task = taskflow.emplace([&]() {
            ZoneScopedN("RenderingStage");
            while (!eval_done || eval_queue.size_approx() > 0) {
                EvaluatedFrame ef;
                if (eval_queue.try_dequeue(ef)) {
                    ZoneScopedN("RenderFrame");
                    auto fb = m_renderer->render_scene(ef.scene, m_composition->camera, m_composition->width(), m_composition->height());
                    render_queue.enqueue({ef.frame, std::move(fb), std::move(ef.arena)});
                } else {
                    std::this_thread::yield();
                }
            }
            render_done = true;
        }).name("Rendering");

        // Stage 3: Output/Encoding
        auto output_task = taskflow.emplace([&]() {
            ZoneScopedN("OutputStage");
            while (!render_done || render_queue.size_approx() > 0) {
                RenderedFrame rf;
                if (render_queue.try_dequeue(rf)) {
                    ZoneScopedN("ProcessOutput");
                    output_callback(std::move(rf));
                    // rf.arena will be destroyed here, releasing memory
                } else {
                    std::this_thread::yield();
                }
            }
        }).name("Output");

        executor.run(taskflow).wait();
    }

private:
    std::shared_ptr<const Composition> m_composition;
    std::shared_ptr<Renderer> m_renderer;
};

} // namespace chronon3d
