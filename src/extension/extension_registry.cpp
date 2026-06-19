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
    // Incremental: only register modules added since the last call.
    // This allows modules to be registered after previous register_all()
    // calls without losing idempotency.
    for (std::size_t i = m_registered_count; i < m_modules.size(); ++i) {
        m_modules[i]->register_all();
    }
    m_registered_count = m_modules.size();
}

bool ExtensionRegistry::contains(std::string_view name) const {
    return std::any_of(m_modules.begin(), m_modules.end(),
        [name](const auto& m) { return m->name() == name; });
}

} // namespace chronon3d
