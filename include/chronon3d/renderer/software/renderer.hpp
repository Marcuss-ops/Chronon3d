#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/renderer/software/framebuffer.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/camera.hpp>
#include <memory>

namespace chronon3d {

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) = 0;
    
    virtual std::unique_ptr<Framebuffer> render_scene(
        const Scene& scene, 
        const Camera& camera, 
        i32 width, i32 height,
        Frame frame = 0
    ) = 0;
};

} // namespace chronon3d
