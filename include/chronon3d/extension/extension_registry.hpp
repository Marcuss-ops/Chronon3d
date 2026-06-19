#pragma once

// ============================================================================
// extension_registry.hpp — Minimal registry for ExtensionModules.
//
// Fase 5: Extension system.
// Tracks loaded extension modules and provides a single `register_all()`
// entry-point that calls each module's `register_all()` in registration order.
//
// Replaces the old ExtensionRegistry (removed in PR 7) which was a dead
// facade.  This version is a simple owning registry with no domain
// indirection.
// ============================================================================

#include <chronon3d/extension/extension_module.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace chronon3d {

/// Owns and orchestrates all extension modules.
///
/// Singleton — use `ExtensionRegistry::instance()`.
///
/// Thread-safety: `register_module()` and `register_all()` are intended
/// for the single-threaded startup phase.  `modules()` is read-only
/// after startup and concurrent-safe.
class ExtensionRegistry {
public:
    static ExtensionRegistry& instance();

    ExtensionRegistry(const ExtensionRegistry&) = delete;
    ExtensionRegistry& operator=(const ExtensionRegistry&) = delete;

    /// Register a module (takes ownership).
    /// Must be called before `register_all()`.
    void register_module(std::unique_ptr<ExtensionModule> module);

    /// Call `register_all()` on every registered module, in registration
    /// order.  Idempotent — subsequent calls only process modules
    /// registered since the last call.
    void register_all();

    /// Read-only access to registered modules (for diagnostics).
    [[nodiscard]] const std::vector<std::unique_ptr<ExtensionModule>>&
    modules() const { return m_modules; }

    /// Check if a module with the given name is registered.
    [[nodiscard]] bool contains(std::string_view name) const;

private:
    ExtensionRegistry() = default;

    std::vector<std::unique_ptr<ExtensionModule>> m_modules;
    std::size_t m_registered_count{0};  // modules [0..count) have been register_all()'d
};

} // namespace chronon3d
