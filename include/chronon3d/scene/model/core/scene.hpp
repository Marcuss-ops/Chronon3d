#pragma once

#include <chronon3d/scene/model/render/render_node.hpp>
#include <filesystem>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/camera/camera_projection_source.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/rendering/depth_grade.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <vector>
#include <memory_resource>
#include <memory>

namespace chronon3d {

// Forward declaration — full type in <chronon3d/scene/model/camera/camera_2_5d.hpp>.
// scene.cpp defines all methods that access Camera2_5DRuntime fields.
struct Camera2_5D;
using Camera2_5DRuntime = Camera2_5D;

class Scene {
public:
    explicit Scene(std::pmr::memory_resource* res = std::pmr::get_default_resource());
    ~Scene();

    // Move-only (unique_ptr member cannot be copied).
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

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

private:
    std::pmr::vector<RenderNode> m_nodes;
    std::pmr::vector<Layer> m_layers;
    std::unique_ptr<Camera2_5DRuntime> m_camera_2_5d;
    rendering::LightContext m_lights{};
    rendering::RimLight m_rim{};
    rendering::DepthGrade m_depth_grade{};
    bool m_hierarchy_baked{false};
    std::filesystem::path m_assets_root;
};

} // namespace chronon3d
