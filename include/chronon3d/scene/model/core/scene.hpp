#pragma once

#include <chronon3d/scene/model/render/render_node.hpp>
#include <filesystem>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/rendering/depth_grade.hpp>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <vector>
#include <memory_resource>
#include <memory>
#include <optional>

namespace chronon3d {

// Forward declaration — full type in <chronon3d/scene/model/camera/camera_2_5d.hpp>.
// scene.cpp defines all methods that access Camera2_5DRuntime fields.
struct Camera2_5D;
using Camera2_5DRuntime = Camera2_5D;

// Forward declarations for the camera facade / shot timeline opaque types
// (defined in their own public headers).  Forward-declaring here keeps the
// Scene header slim and avoids pulling `camera_v1::ShotTimeline`'s transitive
// `CameraSession` include into every TU that touches a Scene.
namespace chronon3d {
class SceneCameraFacade;
namespace camera_v1 { class ShotTimeline; }
class CameraPresetCatalog;
}

class Scene {
public:
    explicit Scene(std::pmr::memory_resource* res = std::pmr::get_default_resource());
    ~Scene();

    // Move-only (unique_ptr member cannot be copied).
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    /// Deep-clone — defined in scene.cpp (requires complete Camera2_5DRuntime).
    [[nodiscard]] Scene clone() const;

    void add_node(RenderNode node) {
        m_nodes.push_back(std::move(node));
    }

    void add_layer(Layer layer) {
        m_layers.push_back(std::move(layer));
    }

    [[nodiscard]] RenderNode& last_node() { return m_nodes.back(); }

    [[nodiscard]] const std::pmr::vector<RenderNode>& nodes() const { return m_nodes; }
    [[nodiscard]] std::pmr::vector<RenderNode>& nodes() { return m_nodes; }
    [[nodiscard]] const std::pmr::vector<Layer>& layers() const { return m_layers; }
    [[nodiscard]] std::pmr::vector<Layer>& layers() { return m_layers; }

    // ── Camera accessors (defined in scene.cpp) ──────────────────────────
    void set_camera_2_5d(Camera2_5DRuntime camera);
    [[nodiscard]] const Camera2_5DRuntime& camera_2_5d() const;

    // ── Public camera facade (P3-H + feat(api) public camera facade) ─────
    //
    // `scene.camera()` returns a chainable facade (BY VALUE) that exposes
    //   .descriptor(d) / .program(p) / .timeline(tl) / .preset(name, cat)
    // setters.  Each setter mutates the scene's internal camera state via
    // a back-reference (the facade stores `Scene&`) and returns
    // `*this` for fluent chaining.  The facade hides `CameraSession` and
    // `RenderGraph` from the public surface — the external consumer never
    // sees those internal types.
    //
    // P3-H bug fix (code-review verdict): the facade is RETURNED BY
    // VALUE, not by reference.  The earlier `SceneCameraFacade&` design
    // required a `std::unique_ptr<SceneCameraFacade>` member on Scene
    // that needed a back-reference to `*this` — chicken-and-egg
    // (member init list cannot reference `*this`).  Return-by-value
    // sidesteps the init-order problem entirely: the facade is a
    // lightweight 1-pointer struct (just `Scene& scene_`) constructed
    // on demand, bound to `*this` at call time.
    //
    // Const overload: removed.  The const overload returned a facade that
    // would dispatch to a `const Scene&` — but every setter on the facade
    // mutates the bound Scene, so a `const` overload is a foot-gun (it
    // would require the const facade to be a different type that
    // const_casts internally — worse than the original bug).  External
    // consumers should always call `scene.camera()` on a non-const Scene
    // (the Scene is owned by the SceneBuilder, so it's always non-const
    // in practice).
    [[nodiscard]] SceneCameraFacade camera() noexcept;

    // ── Internal setters used by `SceneCameraFacade` (P3-H public facade) ──
    // These are the single-source-of-truth for the camera state stored on
    // a Scene.  External callers should reach them through
    // `scene.camera().descriptor(...)` etc. — not directly.
    void set_default_camera_descriptor(camera_v1::CameraDescriptor d) {
        m_camera_descriptor = std::move(d);
    }
    void set_default_camera_program(camera_v1::CameraProgram p) {
        m_camera_program = std::move(p);
    }
    void set_camera_timeline(std::shared_ptr<camera_v1::ShotTimeline> tl) {
        m_camera_timeline = std::move(tl);
    }

    // ── Read-only accessors for the camera state (P3-H public facade) ──
    [[nodiscard]] const std::optional<camera_v1::CameraDescriptor>&
    default_camera_descriptor() const noexcept { return m_camera_descriptor; }
    [[nodiscard]] const std::optional<camera_v1::CameraProgram>&
    default_camera_program() const noexcept { return m_camera_program; }
    [[nodiscard]] const std::shared_ptr<camera_v1::ShotTimeline>&
    camera_timeline() const noexcept { return m_camera_timeline; }

    /// Non-mutating projection source — usable without including camera_2_5d.hpp.
    [[nodiscard]] CameraProjectionSource camera_projection_source() const;

    [[nodiscard]] rendering::LightContext& light_context() { return m_lights; }
    [[nodiscard]] const rendering::LightContext& light_context() const { return m_lights; }

    [[nodiscard]] rendering::RimLight& rim_light() { return m_rim; }
    [[nodiscard]] const rendering::RimLight& rim_light() const { return m_rim; }
    void set_rim_light(const rendering::RimLight& rim) { m_rim = rim; }

    [[nodiscard]] rendering::DepthGrade& depth_grade() { return m_depth_grade; }
    [[nodiscard]] const rendering::DepthGrade& depth_grade() const { return m_depth_grade; }
    void set_depth_grade(const rendering::DepthGrade& grade) { m_depth_grade = grade; }

    // ── Hierarchy resolution (defined in scene.cpp) ──────────────────────
    void resolve_hierarchy(Frame frame);

    [[nodiscard]] bool hierarchy_baked() const { return m_hierarchy_baked; }
    [[nodiscard]] bool hierarchy_resolved() const { return m_hierarchy_baked; }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_nodes.get_allocator().resource(); }

    [[nodiscard]] const std::filesystem::path& assets_root() const { return m_assets_root; }
    void set_assets_root(std::filesystem::path root) { m_assets_root = std::move(root); }

    // Asset manifest: aggregated from all layers during build.
    [[nodiscard]] const assets::AssetManifest& asset_manifest() const { return m_manifest; }
    [[nodiscard]] assets::AssetManifest& asset_manifest() { return m_manifest; }

private:
    std::pmr::vector<RenderNode> m_nodes;
    std::pmr::vector<Layer> m_layers;
    std::unique_ptr<Camera2_5DRuntime> m_camera_2_5d;
    rendering::LightContext m_lights{};
    rendering::RimLight m_rim{};
    rendering::DepthGrade m_depth_grade{};
    bool m_hierarchy_baked{false};
    std::filesystem::path m_assets_root;
    assets::AssetManifest m_manifest;

    // P3-H public camera facade — internal camera state stored on the
    // Scene.  Mutated only via the `SceneCameraFacade` setters.  The
    // facade is RETURNED BY VALUE from `Scene::camera()` (see comment
    // on the public method above), so no facade member is needed here.
    std::optional<camera_v1::CameraDescriptor> m_camera_descriptor;
    std::optional<camera_v1::CameraProgram>    m_camera_program;
    std::shared_ptr<camera_v1::ShotTimeline>   m_camera_timeline;
};

} // namespace chronon3d
