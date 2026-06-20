// type_checker.hpp — Path B expression type checker.
//
// Static type analysis of AST nodes. Strict on asymmetric arithmetic between
// fully-inferred scalar/vector types (e.g. `1 + "foo"` errors). Permissive
// on the Type::Top family: any binary arithmetic where AT LEAST ONE operand
// is Top returns Top without erroring.
//
// === Trade-off: Type::Top permissiveness ===
//
// After Effects expressions routinely reference names whose effective type
// is only known at runtime (free property bindings, `thisComp` member chains
// that go through a layer ref, unresolved index lookups, etc.).  These all
// classify as Type::Top in our static checker.  Rejecting `x + 1` outright
// would mean every host-authored expression containing a free identifier
// needs explicit refinement upstream — not viable.  Instead we accept the
// expression and emit Top; the VM surfaces a runtime diagnostic if the
// actual value's type is incompatible.
//
// Concretely:  `true + x`  → Top  (Bool relaxed by free identifier x)
//              `"" + x`   → Top  (String relaxed by free identifier x)
//              `1 + "foo"` → ERROR (no Top involved; both types known)
//              `x * 2`    → Top  (Top relaxed by Number — happy-path
//                                 variable-binding test)
//
// Vec+Vec widens to Vec via widen()'s same-type clause (so
// `thisLayer.position + thisLayer.scale` type-checks as Vec3 — NOT Top).
// Number+Vector (e.g. `position * 2` where position static-type is Top
// because it is unbound at compile time) lands in the Top-or-Top branch
// and emits Top.  Returns a `Type` per node + a list of diagnostics for
// downstream stages (compiler/VM) to surface.

#pragma once

#include <chronon3d_experimental/expressions/v2/ast.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::expressions::v2 {

// ── Static type tags returned by the checker ─────────────────────────
enum class Type : std::uint8_t {
    Unknown = 0,   // error / uninferrable
    Number,
    String,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Color,
    LayerReference,
    PropertyReference,
    Top,           // any compatible type (permissive fallback)
};

[[nodiscard]] const char* type_name(Type t) noexcept;

// ── Diagnostics ──────────────────────────────────────────────────────
struct TypeError {
    SourceSpan span{};
    std::string message;
};

// ── Type-check an entire AST (component_or_union) ───────────────────
// Returns the inferred type of `root`. Use `errors` to surface diagnostics.
struct TypeCheckResult {
    Type                         root_type{Type::Unknown};
    std::vector<TypeError>       errors;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] TypeCheckResult type_check(const AstNode& root) noexcept;

// ── Helpers exposed for tests/diagnostics ────────────────────────────
[[nodiscard]] bool is_vector_type(Type t) noexcept;
[[nodiscard]] bool is_numeric_type(Type t) noexcept;

} // namespace chronon3d::expressions::v2
