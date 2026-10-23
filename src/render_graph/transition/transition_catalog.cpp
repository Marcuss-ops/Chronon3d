#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include "transition_catalog_registration_detail.hpp"

#include <stdexcept>
#include <utility>

namespace chronon3d::graph {

void LayerTransitionCatalog::register_transition(std::string id, Factory factory) {
    if (id.empty()) {
        throw std::invalid_argument("LayerTransitionCatalog: transition id cannot be empty");
    }
    m_factories.emplace(std::move(id), std::move(factory));
}

bool LayerTransitionCatalog::contains(std::string_view id) const {
    // std::less<> enables heterogeneous lookup; avoid allocating a temporary
    // std::string for every capability/preflight probe.
    return m_factories.find(id) != m_factories.end();
}

LayerTransitionCatalog::BackendCapability
LayerTransitionCatalog::backend_capability(std::string_view id) const {
    // Canonical id→capability mapping lives here, next to the registration
    // that owns the same id. Consumers must not dispatch on string ids.
    if (id == "crossfade" || id == "fly_under_fade") {
        return BackendCapability::NativeBlend;
    }
    return BackendCapability::CpuOnly;
}

std::unique_ptr<LayerTransitionProgram> LayerTransitionCatalog::resolve(const LayerTransitionSpec& spec) const {
    auto it = m_factories.find(spec.transition_id);
    if (it == m_factories.end()) {
        throw std::runtime_error("Unknown layer transition: " + spec.transition_id);
    }
    return it->second(spec);
}



void LayerTransitionCatalog::register_builtin(LayerTransitionCatalog& catalog) {
    catalog.register_transition("none", [](const LayerTransitionSpec& spec) { return make_none(spec); });
    catalog.register_transition("crossfade", [](const LayerTransitionSpec& spec) { return make_crossfade(spec); });
    catalog.register_transition("dip_to_black", [](const LayerTransitionSpec& spec) { return make_dip_to_black(spec); });
    catalog.register_transition("slide", [](const LayerTransitionSpec& spec) { return make_slide(spec); });
    catalog.register_transition("zoom", [](const LayerTransitionSpec& spec) { return make_zoom(spec); });
    catalog.register_transition("wipe_linear", [](const LayerTransitionSpec& spec) { return make_wipe_linear(spec); });
    catalog.register_transition("smooth_wipe", [](const LayerTransitionSpec& spec) { return make_smooth_wipe(spec); });
    catalog.register_transition("circle_iris", [](const LayerTransitionSpec& spec) { return make_circle_iris(spec); });
    catalog.register_transition("flash", [](const LayerTransitionSpec& spec) { return make_flash(spec); });
    catalog.register_transition("procedural_remotion", [](const LayerTransitionSpec& spec) { return make_procedural_remotion(spec); });
    catalog.register_transition("remotion", [](const LayerTransitionSpec& spec) { return make_remotion(spec); });
    catalog.register_transition("fly_under_fade", [](const LayerTransitionSpec& spec) { return make_fly_under_fade(spec); });
}

} // namespace chronon3d::graph
