// ═══════════════════════════════════════════════════════════════════════════
// node_handle.hpp — explicit handle for node-level transforms.
//
// A4 — replaces the implicit "last node" pattern where `.at()`,
// `.scale_node()`, `.rotate_node()`, `.anchor_node()`, `.node_opacity()`,
// `.with_shadow()`, `.with_glow()` all mutated whichever node happened
// to be the last one pushed into the layer/scene.
//
// With NodeHandle, every shape-creation method that returns a handle
// gives the caller an explicit reference to the node they just created:
//
//   auto card = layer.rect("card", {.size = {600, 300}});
//   card.position({960, 540, 0})
//       .rotation({0, 0, 5})
//       .opacity(0.8f);
//
// For backward compatibility, the old implicit methods are marked
// [[deprecated]] and the explicit `last_node_handle()` accessor is
// available as an opt-in migration path.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d {

/// Non-owning handle to a single RenderNode.  Provides fluent setters
/// for position, scale, rotation, anchor, opacity, shadow, and glow.
/// Returned by shape-creation methods and `last_node_handle()`.
class NodeHandle {
public:
    explicit NodeHandle(RenderNode& node) noexcept : node_(&node) {}

    NodeHandle(const NodeHandle&)            = default;
    NodeHandle& operator=(const NodeHandle&) = default;

    // ── Transform ──────────────────────────────────────────────────────

    NodeHandle& position(Vec3 pos) {
        node_->world_transform.position = pos;
        return *this;
    }

    NodeHandle& scale(Vec3 s) {
        node_->world_transform.scale = s;
        return *this;
    }

    NodeHandle& rotate(Vec3 euler_deg) {
        node_->world_transform.rotation = glm::quat(glm::radians(euler_deg));
        return *this;
    }

    NodeHandle& anchor(Vec3 a) {
        node_->world_transform.anchor = a;
        return *this;
    }

    NodeHandle& opacity(f32 value) {
        node_->world_transform.opacity = value;
        return *this;
    }

    // ── Polish ─────────────────────────────────────────────────────────

    NodeHandle& with_shadow(DropShadow shadow) {
        node_->shadow = shadow;
        return *this;
    }

    NodeHandle& with_glow(Glow glow) {
        node_->glow = glow;
        return *this;
    }

    // ── Read-only access ───────────────────────────────────────────────

    [[nodiscard]] const Transform& transform() const noexcept { return node_->world_transform; }
    [[nodiscard]] RenderNode&      node()            noexcept { return *node_; }

private:
    RenderNode* node_;
};

} // namespace chronon3d
