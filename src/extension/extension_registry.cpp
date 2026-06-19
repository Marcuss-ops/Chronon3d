// ============================================================================
// extension_registry.cpp — ExtensionRegistry implementation.
// ============================================================================

#include <chronon3d/extension/extension_registry.hpp>
#include <algorithm>
#include <stdexcept>

namespace chronon3d {

ExtensionRegistry& ExtensionRegistry::instance() {
    static ExtensionRegistry s_instance;
    return s_instance;
}

void ExtensionRegistry::register_module(
    std::unique_ptr<ExtensionModule> module)
{
    if (!module) return;
    if (contains(module->name())) {
        throw std::logic_error(
            "ExtensionModule with duplicate name: "
            + std::string(module->name()));
    }
    m_modules.push_back(std::move(module));
}

void ExtensionRegistry::register_all() {
    if (m_registered) return;
    for (auto& mod : m_modules) {
        mod->register_all();
    }
    m_registered = true;
}

bool ExtensionRegistry::contains(std::string_view name) const {
    return std::any_of(m_modules.begin(), m_modules.end(),
        [name](const auto& m) { return m->name() == name; });
}

} // namespace chronon3d
