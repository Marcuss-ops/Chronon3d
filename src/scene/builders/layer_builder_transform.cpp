// Node-level explicit access.

#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d {

NodeHandle LayerBuilder::last_node_handle() {
    if (m_layer.nodes.empty()) {
        static RenderNode sentinel;
        return NodeHandle(sentinel);
    }
    return NodeHandle(m_layer.nodes.back());
}

} // namespace chronon3d
