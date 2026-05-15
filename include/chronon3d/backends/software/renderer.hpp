#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <memory>
#include <optional>

namespace chronon3d {

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual std::shared_ptr<Framebuffer> render_scene(const Scene& scene,
                                                      const Camera& camera,
                                                      i32 width,
                                                      i32 height) = 0;

    virtual std::shared_ptr<Framebuffer> render_scene(const Scene& scene,
                                                      const std::optional<Camera2_5D>& camera,
                                                      i32 width,
                                                      i32 height) = 0;
};

} // namespace chronon3d
