#pragma once

#include <chronon3d/scene/model/scene.hpp>
#include <chronon3d/scene/model/camera.hpp>
#include <chronon3d/scene/model/camera_2_5d.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <memory>
#include <optional>

namespace chronon3d {

class Renderer {
public:
    Renderer() = default;
    virtual ~Renderer() = default;
    Renderer(const Renderer&) = default;
    Renderer& operator=(const Renderer&) = default;
    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;

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
