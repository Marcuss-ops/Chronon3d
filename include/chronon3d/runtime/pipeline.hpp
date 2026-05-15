#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/core/arena.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace chronon3d {

struct EvaluatedFrame {
    Frame frame;
    std::shared_ptr<FrameArena> arena;
    Scene scene;
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

    void run(Frame start, Frame end, std::function<void(RenderedFrame)> output_callback);

private:
    std::shared_ptr<const Composition> m_composition;
    std::shared_ptr<Renderer> m_renderer;
};

} // namespace chronon3d
