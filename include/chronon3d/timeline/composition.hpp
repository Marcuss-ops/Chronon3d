#pragma once

#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/scene/camera.hpp>
#include <chronon3d/scene/scene.hpp>
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace chronon3d {

struct CompositionSpec {
    std::string name{"Untitled"};
    i32 width{1920};
    i32 height{1080};
    FrameRate frame_rate{30, 1};
    Frame duration{0};
};

class Composition {
public:
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    Composition(CompositionSpec spec, SceneFunction render)
        : m_spec(std::move(spec)), m_render(std::move(render)) {}

    [[nodiscard]] i32 width() const { return m_spec.width; }
    [[nodiscard]] i32 height() const { return m_spec.height; }
    [[nodiscard]] FrameRate frame_rate() const { return m_spec.frame_rate; }
    [[nodiscard]] Frame duration() const { return m_spec.duration; }
    [[nodiscard]] const std::string& name() const { return m_spec.name; }

    [[nodiscard]] Scene evaluate(Frame frame,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate(frame, 0.0f, res);
    }

    // Evaluate at a fractional frame offset (used by motion blur subsampling).
    [[nodiscard]] Scene evaluate(Frame frame, f32 frame_time,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        FrameContext ctx{
            .frame      = frame,
            .frame_time = frame_time,
            .duration   = m_spec.duration,
            .frame_rate = m_spec.frame_rate,
            .width      = m_spec.width,
            .height     = m_spec.height,
            .resource   = res
        };
        return m_render(ctx);
    }

    Camera camera;

private:
    CompositionSpec m_spec;
    SceneFunction m_render;
};

inline Composition composition(CompositionSpec spec, Composition::SceneFunction render) {
    return Composition(std::move(spec), std::move(render));
}

} // namespace chronon3d
