// Node transform and internal helper implementations for LayerBuilder.
// Extracted from layer_builder.cpp to reduce file size.
//
// A4 — suppressed deprecation warnings: the .at()/.scale_node()/etc.
// implementations are the canonical bodies for the deprecated methods.
// Call sites see the deprecation; the implementation does not.

#include <chronon3d/scene/builders/layer_builder.hpp>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

namespace chronon3d {

namespace layer_builder_internal {

RenderNode* last_node(Layer& layer) {
    if (layer.nodes.empty()) {
        return nullptr;
    }
    return &layer.nodes.back();
}

void set_last_shadow(Layer& layer, DropShadow shadow) {
    if (auto* node = last_node(layer)) {
        node->shadow = shadow;
    }
}

void set_last_glow(Layer& layer, Glow glow) {
    if (auto* node = last_node(layer)) {
        node->glow = glow;
    }
}

void set_last_position(Layer& layer, Vec3 pos) {
    if (auto* node = last_node(layer)) {
        node->world_transform.position = pos;
    }
}

void set_last_rotation(Layer& layer, Vec3 euler_deg) {
    if (auto* node = last_node(layer)) {
        node->world_transform.rotation = glm::quat(glm::radians(euler_deg));
    }
}

void set_last_scale(Layer& layer, Vec3 s) {
    if (auto* node = last_node(layer)) {
        node->world_transform.scale = s;
    }
}

void set_last_anchor(Layer& layer, Vec3 a) {
    if (auto* node = last_node(layer)) {
        node->world_transform.anchor = a;
    }
}

void set_last_opacity(Layer& layer, f32 opacity) {
    if (auto* node = last_node(layer)) {
        node->world_transform.opacity = opacity;
    }
}

} // namespace layer_builder_internal

// ── Node Transform ────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::at(Vec3 pos) {
    layer_builder_internal::set_last_position(m_layer, pos);
    return *this;
}

LayerBuilder& LayerBuilder::rotate_node(Vec3 euler_deg) {
    layer_builder_internal::set_last_rotation(m_layer, euler_deg);
    return *this;
}

LayerBuilder& LayerBuilder::scale_node(Vec3 s) {
    layer_builder_internal::set_last_scale(m_layer, s);
    return *this;
}

LayerBuilder& LayerBuilder::anchor_node(Vec3 a) {
    layer_builder_internal::set_last_anchor(m_layer, a);
    return *this;
}

LayerBuilder& LayerBuilder::node_opacity(f32 a) {
    layer_builder_internal::set_last_opacity(m_layer, a);
    return *this;
}

// ── A4 — Explicit node handle accessor ───────────────────────────

NodeHandle LayerBuilder::last_node_handle() {
    // Return a handle to the last node.  When the node list is empty,
    // return a handle to a static sentinel RenderNode — mutations are
    // harmless no-ops and the caller can check .node_count() == 0 on
    // the layer if they want to guard against this edge case.
    if (m_layer.nodes.empty()) {
        static RenderNode sentinel;
        return NodeHandle(sentinel);
    }
    return NodeHandle(m_layer.nodes.back());
}

} // namespace chronon3d

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
