#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <memory>

namespace chronon3d {

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual std::shared_ptr<Framebuffer> render_scene(const Scene& scene, 
                                                      const std::optional<Camera2_5D>& camera,
                                                      int width, int height) = 0;
};

} // namespace chronon3d
