#include <chronon3d/runtime/bench_runner.hpp>
#include <chrono>

namespace chronon3d::runtime {

BenchResult BenchRunner::run(const std::string& comp_id,
                             Composition& comp,
                             Renderer& renderer,
                             int frames,
                             int warmup) {
    for (int i = 0; i < warmup; ++i) {
        const auto frame = static_cast<Frame>(i);
        auto scene = comp.evaluate(frame);
        renderer.render_scene(scene, comp.camera, comp.width(), comp.height());
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        const auto frame = static_cast<Frame>(warmup + i);
        auto scene = comp.evaluate(frame);
        renderer.render_scene(scene, comp.camera, comp.width(), comp.height());
    }
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    const double avg_ms = elapsed_ms / static_cast<double>(frames);
    const double fps = 1000.0 / avg_ms;

    return BenchResult{
        .comp_id = comp_id,
        .frames = frames,
        .warmup = warmup,
        .total_elapsed_ms = elapsed_ms,
        .avg_ms = avg_ms,
        .fps = fps
    };
}

} // namespace chronon3d::runtime
