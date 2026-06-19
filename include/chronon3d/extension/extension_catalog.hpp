#pragma once

// ============================================================================
// extension_catalog.hpp — Value-type catalog for ExtensionModules.
//
// Replaces the old singleton ExtensionRegistry.  Create an instance,
// register modules, call register_all(ctx), and hand ownership to the
// caller (e.g. PipelineCatalogs).
//
// Thread-safety: register_module() and register_all() are intended
// for the single-threaded startup phase.  modules() is read-only
// after startup and concurrent-safe.
// ============================================================================

#include <chronon3d/extension/extension_module.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace chronon3d {

struct ExtensionContext;

/// Owns and orchestrates all extension modules.
///
/// Plain value type — create one, populate it, freeze it (via
/// register_all), then pass it by const-ref.
///
///     ExtensionCatalog catalog;
///     catalog.register_module(...);
///     catalog.register_all(ctx);
///
class ExtensionCatalog {
public:
    ExtensionCatalog() = default;

    ExtensionCatalog(const ExtensionCatalog&) = delete;
    ExtensionCatalog& operator=(const ExtensionCatalog&) = delete;
    ExtensionCatalog(ExtensionCatalog&&) noexcept = default;
    ExtensionCatalog& operator=(ExtensionCatalog&&) noexcept = default;

    /// Register a module (takes ownership).
    void register_module(std::unique_ptr<ExtensionModule> module);

    /// Call register_all(ctx) on every registered module, in registration
    /// order.  Idempotent — subsequent calls only process modules
    /// registered since the last call.
    void register_all(ExtensionContext& ctx);

    /// Read-only access to registered modules (for diagnostics).
    [[nodiscard]] const std::vector<std::unique_ptr<ExtensionModule>>&
    modules() const { return m_modules; }

    /// Check if a module with the given name is registered.
    [[nodiscard]] bool contains(std::string_view name) const;

private:
    std::vector<std::unique_ptr<ExtensionModule>> m_modules;
    std::size_t m_registered_count{0};
};

} // namespace chronon3d
