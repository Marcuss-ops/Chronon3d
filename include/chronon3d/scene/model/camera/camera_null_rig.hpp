#pragma once

#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <string>
#include <vector>

namespace chronon3d {

// Forward declarations
class SceneBuilder;

// ---------------------------------------------------------------------------
// CameraNullNode — A single null object in the parent chain with animated
// position and rotation (Euler degrees). Each node can be independently
// keyframed, and the parent chain creates the classic After Effects-style
// multi-null camera rig where each null controls a different axis of motion.
// ---------------------------------------------------------------------------
struct CameraNullNode {
    std::string name;
    AnimatedValue<Vec3> position{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> rotation{Vec3{0.0f, 0.0f, 0.0f}}; // Euler degrees
    AnimatedValue<Vec3> scale{Vec3{1.0f, 1.0f, 1.0f}};

    bool inherits_position{true};
    bool inherits_rotation{true};
    bool inherits_scale{true};
    bool visible_in_diagnostics{true};

    CameraNullNode() = default;

    CameraNullNode(std::string name_)
        : name(std::move(name_)) {}

    // Evaluate the node at a given frame and return a Transform3D
    [[nodiscard]] Transform3D evaluate(Frame frame) const;
};

// ---------------------------------------------------------------------------
// CameraNullRig — A chain of null objects with a camera at the end.
//
// In After Effects, you can stack 4-5 null objects in a parent chain to
// control different aspects of camera movement independently (e.g., one null
// for X/Y motion, another for Z/dolly, another for orbit/rotation, another
// for roll). This rig replicates that workflow.
//
// The evaluation builds the null hierarchy from first to last, with each
// null parenting to the previous one. The camera is positioned at the end
// of the chain, inheriting all accumulated transforms.
// ---------------------------------------------------------------------------
struct CameraNullRig {
    std::string name{"CameraNullRig"};

    // The chain of null objects (ordered from root to leaf).
    // nulls[0] has no parent (or an optional root_parent).
    // Each subsequent null parents to the previous one.
    // The camera parents to the last null.
    std::vector<CameraNullNode> nodes;

    // Optional root parent name (if the entire chain should be under an
    // existing layer/null in the scene).
    std::string root_parent_name;

    // Point of Interest — if enabled, the camera will use lookAt towards
    // this point (Two-Node mode).
    bool point_of_interest_enabled{false};
    AnimatedValue<Vec3> point_of_interest{Vec3{0.0f, 0.0f, 0.0f}};

    // Camera properties (applied on top of the null chain transform).
    AnimatedValue<Vec3> camera_position_offset{Vec3{0.0f, 0.0f, -1000.0f}};
    AnimatedValue<Vec3> camera_rotation_offset{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<f32> zoom{1000.0f};
    AnimatedValue<f32> fov_deg{50.0f};
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};

    // Depth of Field
    AnimatedValue<bool> dof_enabled{false};
    AnimatedValue<f32> focus_z{0.0f};
    AnimatedValue<f32> aperture{0.015f};
    AnimatedValue<f32> max_blur{24.0f};

    // Motion Blur
    AnimatedValue<bool> motion_blur_enabled{false};
    AnimatedValue<int> motion_blur_samples{8};
    AnimatedValue<f32> shutter_angle{180.0f};

    CameraNullRig() = default;

    explicit CameraNullRig(std::string name_)
        : name(std::move(name_)) {}

    // Add a null node to the end of the chain.
    CameraNullNode& add_node(std::string node_name) {
        return nodes.emplace_back(std::move(node_name));
    }

    // Add a null node and return a reference to configure it.
    CameraNullNode& add_node() {
        return nodes.emplace_back();
    }

    // Number of nulls in the chain.
    [[nodiscard]] size_t node_count() const { return nodes.size(); }

    // Evaluate the entire rig at a given frame, returning the final
    // Camera2_5D with all null transforms accumulated.
    [[nodiscard]] Camera2_5D evaluate(Frame frame) const;

    // Build all null layers and camera setup in the scene using SceneBuilder.
    void build(SceneBuilder& scene, Frame frame) const;
};

} // namespace chronon3d
