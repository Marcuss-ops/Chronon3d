// type_checker.hpp — Path B expression type checker.
//
// Static type analysis of AST nodes. Permissive on Vec/Color mixing (we do
// not yet have full Vec-op-Vec or Color+Number); strict on asymmetric
// arithmetic (e.g. `1 + "foo"`). Returns a `Type` per node + a list of
// diagnostics for downstream stages (compiler/VM) to surface.

#pragma once

#include <chronon3d/expressions/v2/ast.hpp>

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
