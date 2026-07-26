// Node-level explicit access and the single generic motion preset entry point.

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/presets/motion_preset_packs.hpp>

namespace chronon3d {

NodeHandle LayerBuilder::last_node_handle() {
    if (m_layer.nodes.empty()) {
        static RenderNode sentinel;
        return NodeHandle(sentinel);
    }
    return NodeHandle(m_layer.nodes.back());
}

LayerBuilder& LayerBuilder::motion(
    std::string_view preset_id,
    const presets::MotionParameters& params) {
    presets::motion_preset_catalog().apply(*this, preset_id, params);
    return *this;
}

} // namespace chronon3d
