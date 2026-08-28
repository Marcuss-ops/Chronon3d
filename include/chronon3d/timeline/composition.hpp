#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <cstdint>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <functional>
#include <atomic>
#include <string>
#include <memory>
#include <utility>

namespace chronon3d {

template <typename T, typename E> class Result;
struct CompiledComposition;
struct CompositionCompileContext;
struct CompositionCompileError;

struct CompositionSpec {
    std::string name{"Untitled"};
    i32 width{1920};
    i32 height{1080};
    FrameRate frame_rate{30, 1};
    Frame duration{0};
};

/// Canonical immutable composition value shared by authoring, registry and
/// compilation boundaries. DTOs may describe requests/metadata at external
/// boundaries, but the core never stores a second composition model.
class Composition {
public:
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    Composition(CompositionSpec spec,
                SceneFunction render,
                std::uint64_t scene_content_fingerprint = 0)
        : m_spec(std::move(spec)), m_render(std::move(render)),
          m_scene_content_fingerprint(scene_content_fingerprint),
          m_identity(s_next_identity.fetch_add(1, std::memory_order_relaxed)) {}

    [[nodiscard]] i32 width() const { return m_spec.width; }
    [[nodiscard]] i32 height() const { return m_spec.height; }
    [[nodiscard]] FrameRate frame_rate() const { return m_spec.frame_rate; }
    [[nodiscard]] Frame duration() const { return m_spec.duration; }
    [[nodiscard]] const std::string& name() const { return m_spec.name; }
    [[nodiscard]] const CompositionSpec& spec() const noexcept { return m_spec; }
    [[nodiscard]] std::uint64_t scene_content_fingerprint() const noexcept {
        return m_scene_content_fingerprint;
    }
    [[nodiscard]] std::uint64_t identity() const noexcept { return m_identity; }
    [[nodiscard]] Scene evaluate(const FrameContext& ctx) const {
        Scene result = m_render(ctx);
        if (!ctx.assets_root.empty()) result.set_assets_root(ctx.assets_root);
        return result;
    }

    [[nodiscard]] const SceneFunction& scene_function() const noexcept { return m_render; }

    Composition& default_camera_descriptor(
        camera_v1::CameraDescriptor descriptor) {
        m_default_camera_desc = std::move(descriptor);
        return *this;
    }
    [[nodiscard]] const camera_v1::CameraDescriptor&
    default_camera_descriptor() const noexcept { return m_default_camera_desc; }
    [[nodiscard]] bool has_default_camera_descriptor() const noexcept {
        return !m_default_camera_desc.id.empty();
    }

private:
    friend Result<CompiledComposition, CompositionCompileError>
    compile_composition(const Composition&, const CompositionCompileContext&);

    [[nodiscard]] bool has_scene_function() const noexcept {
        return static_cast<bool>(m_render);
    }
    [[nodiscard]] SceneFunction scene_function_snapshot() const { return m_render; }

    camera_v1::CameraDescriptor m_default_camera_desc{};
    std::uint64_t m_scene_content_fingerprint{0};
    inline static std::atomic_uint64_t s_next_identity{1};
    std::uint64_t m_identity{0};
    CompositionSpec m_spec;
    SceneFunction m_render;
};

inline Composition composition(CompositionSpec spec, Composition::SceneFunction render) {
    return Composition(std::move(spec), std::move(render));
}

} // namespace chronon3d
