#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/core/arena.hpp>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <taskflow/taskflow.hpp>
#include <chronon3d/core/profiling.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

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

        // Reserve ~2 threads for eval and output; the rest render in parallel.
        const unsigned hw = std::thread::hardware_concurrency();
        const unsigned n_workers = std::max(1u, hw > 2u ? hw - 2u : 1u);
        std::atomic<unsigned> workers_finished{0};

        // Stage 1: Evaluation — single sequential task, composition is not thread-safe.
        taskflow.emplace([&]() {
            ZoneScopedN("EvaluationStage");
            for (i64 f = start; f < end; ++f) {
                ZoneScopedN("EvaluateFrame");
                auto arena = std::make_shared<FrameArena>();
                Scene scene = m_composition->evaluate(Frame{f}, arena->resource());
                eval_queue.enqueue({Frame{f}, std::move(scene), std::move(arena)});
            }
            eval_done.store(true, std::memory_order_release);
        }).name("Evaluation");

        // Stage 2: Rendering — N parallel workers drain eval_queue concurrently.
        // render_scene() creates a fresh Framebuffer per call and has no mutable state,
        // so calling it from multiple threads simultaneously is safe.
        for (unsigned w = 0; w < n_workers; ++w) {
            taskflow.emplace([&]() {
                ZoneScopedN("RenderWorker");
                while (!eval_done.load(std::memory_order_acquire) || eval_queue.size_approx() > 0) {
                    EvaluatedFrame ef;
                    if (eval_queue.try_dequeue(ef)) {
                        ZoneScopedN("RenderFrame");
                        auto fb = m_renderer->render_scene(
                            ef.scene,
                            m_composition->camera,
                            m_composition->width(),
                            m_composition->height());
                        render_queue.enqueue({ef.frame, std::move(fb), std::move(ef.arena)});
                    } else {
                        std::this_thread::yield();
                    }
                }
                // Last worker to finish signals the output stage.
                if (workers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == n_workers) {
                    render_done.store(true, std::memory_order_release);
                }
            }).name("RenderWorker");
        }

        // Stage 3: Output/Encoding — single sequential task preserves callback safety.
        taskflow.emplace([&]() {
            ZoneScopedN("OutputStage");
            while (!render_done.load(std::memory_order_acquire) || render_queue.size_approx() > 0) {
                RenderedFrame rf;
                if (render_queue.try_dequeue(rf)) {
                    ZoneScopedN("ProcessOutput");
                    output_callback(std::move(rf));
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
