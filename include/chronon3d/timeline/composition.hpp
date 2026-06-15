#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/assets/asset_registry.hpp>
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
    std::string assets_root{""};
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
    [[nodiscard]] const std::string& assets_root() const { return m_spec.assets_root; }

    [[nodiscard]] Scene evaluate(Frame frame,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate(frame, 0.0f, res);
    }

    /// Sub-frame evaluation (SampleTime) — enables true multi-sample motion blur.
    [[nodiscard]] Scene evaluate(SampleTime time,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate_double(time.frame, time.frame_rate, res);
    }

    // Evaluate at a fractional frame offset (used by motion blur subsampling).
    [[nodiscard]] Scene evaluate(Frame frame, f32 frame_time,
                                 std::pmr::memory_resource* res = std::pmr::get_default_resource()) const {
        return evaluate_double(static_cast<double>(frame) + static_cast<double>(frame_time),
                               m_spec.frame_rate, res);
    }

private:
    [[nodiscard]] Scene evaluate_double(double frame, FrameRate rate,
                                        std::pmr::memory_resource* res) const {
        const Frame integral = static_cast<Frame>(std::floor(frame));
        FrameContext ctx{
            .frame      = integral,
            .local_frame = integral,
            .frame_time = static_cast<f32>(frame - std::floor(frame)),
            .duration   = m_spec.duration,
            .frame_rate = rate,
            .width      = m_spec.width,
            .height     = m_spec.height,
            .assets_root = m_spec.assets_root,
            .resource   = res
        };
        // No longer calling AssetRegistry::mount() globally — the assets root
        // is threaded through FrameContext → Scene → RenderGraphContext →
        // thread-local guard, so concurrent render jobs don't interfere.
        Scene result = m_render(ctx);
        if (!ctx.assets_root.empty()) {
            result.set_assets_root(ctx.assets_root);
        }
        return result;
    }

public:

    Camera camera;

private:
    CompositionSpec m_spec;
    SceneFunction m_render;
};

inline Composition composition(CompositionSpec spec, Composition::SceneFunction render) {
    return Composition(std::move(spec), std::move(render));
}

} // namespace chronon3d
