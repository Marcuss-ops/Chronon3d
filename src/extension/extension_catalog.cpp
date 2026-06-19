// ============================================================================
// extension_catalog.cpp — ExtensionCatalog implementation.
// ============================================================================

#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <algorithm>
#include <stdexcept>

namespace chronon3d {

void ExtensionCatalog::register_module(
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

void ExtensionCatalog::register_all(ExtensionContext& ctx) {
    for (std::size_t i = m_registered_count; i < m_modules.size(); ++i) {
        m_modules[i]->register_all(ctx);
    }
    m_registered_count = m_modules.size();
}

bool ExtensionCatalog::contains(std::string_view name) const {
    return std::any_of(m_modules.begin(), m_modules.end(),
        [name](const auto& m) { return m->name() == name; });
}

} // namespace chronon3d
