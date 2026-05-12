#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
#include <memory>

namespace chronon3d {

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) = 0;
};

} // namespace chronon3d
