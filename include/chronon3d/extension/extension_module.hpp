#pragma once

// ============================================================================
// extension_module.hpp — Abstract base for Chronon3d extension modules.
//
// Fase 5: Extension system.
// Modules inherit ExtensionModule to register features (graph nodes,
// compositions, effects) without touching the core engine.
//
// Usage:
//   class MyModule final : public ExtensionModule {
//       std::string_view name() const override { return "my_module"; }
//       void register_all(ExtensionContext& ctx) override {
//           ctx.compositions.add("MyComp", [] { return make_my_comp(); });
//       }
//   };
// ============================================================================

#include <string_view>

namespace chronon3d {

struct ExtensionContext;

/// Abstract base class for an extension module.
///
/// Each content domain or feature set inherits from this class and
/// implements `register_all(ExtensionContext&)` to wire its factories
/// into the appropriate registries (GraphNodeCatalog, CompositionRegistry,
/// etc.).
///
/// Thread-safety: `register_all()` is called during engine startup
/// (single-threaded phase).  Subsequent access to registries is
/// concurrent-safe (read-only after startup).
class ExtensionModule {
public:
    virtual ~ExtensionModule() = default;

    /// Human-readable module identifier (e.g. "minimalist", "text", "2d5").
    /// Must be unique across all registered modules.
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Called exactly once at engine startup to register this module's
    /// factories into the appropriate registries.  Receives an
    /// ExtensionContext with references to all domain registries.
    /// Must be idempotent (safe to call multiple times).
    virtual void register_all(ExtensionContext& context) = 0;
};

} // namespace chronon3d
