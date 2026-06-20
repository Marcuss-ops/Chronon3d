// expression_value.hpp — Path B ExpressionValue std::variant
//
// Core value type for the v2 expression engine (Path B). Tagged-union of the
// primitives the evaluator can return, plus opaque reference types the host
// (Scene) resolves at evaluation time.
//
// LayerReference and PropertyReference are *opaque* in v2: they carry stable
// identity (string ids, paths) so the compiler/AST/bytecode/VM can reason
// about them, but actual host-side resolution (which property of which layer
// has which value) is the host's job via env callbacks at VM run time.
//
// Path B does NOT extend the existing scalar parser in AnimatedValue; this
// header introduces a fresh, AE-oriented value space. Path A remains on
// main (Path B lives on `feature/expressions-v2` until merge post-freeze).

#pragma once

#include <chronon3d/math/glm_types.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace chronon3d::expressions::v2 {

// ── Opaque reference types (resolved by host via env callbacks) ───────
struct LayerReference {
    std::string layer_id;

    bool operator==(const LayerReference& o) const noexcept {
        return layer_id == o.layer_id;
    }
    bool operator!=(const LayerReference& o) const noexcept {
        return layer_id != o.layer_id;
    }
};

struct PropertyReference {
    std::string layer_id;
    std::string property_path;  // dot-path inside the layer: "position.x"

    bool operator==(const PropertyReference& o) const noexcept {
        return layer_id == o.layer_id && property_path == o.property_path;
    }
    bool operator!=(const PropertyReference& o) const noexcept {
        return !(*this == o);
    }
};

// ── Vector / Color type aliases live in glm_types (Vec2..Vec4) and math/color.hpp
//     We forward-declare-include math/color via transitive includes that the
//     compiler will resolve later. For now, the variant uses glm vec types
//     for the spatial slots; Color lives in chronon3d::Color.
//
// NOTE: chronon3d::Color is f32-based (rgba). When the consumer adds it to
// the variant we get a four-float component ordering that is consistent
// with the existing animated_value.hpp path.
struct Color {
    f32 r{0.0f};
    f32 g{0.0f};
    f32 b{0.0f};
    f32 a{1.0f};

    Color() = default;
    Color(f32 rr, f32 gg, f32 bb, f32 aa = 1.0f) : r(rr), g(gg), b(bb), a(aa) {}

    bool operator==(const Color& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color& o) const noexcept {
        return !(*this == o);
    }
};

// ── ExpressionValue: the std::variant. Order preserved for ABI. ───────
using ExpressionValue = std::variant<
    double,             // number
    bool,               // bool
    std::string,        // string
    Vec2,               // 2-vector
    Vec3,               // 3-vector (treated as Vec<3>)
    Vec4,               // 4-vector
    Color,              // rgba color
    LayerReference,     // opaque layer id
    PropertyReference   // opaque layer+prop
>;

// ── Kind tag (cheap to query without visiting the variant) ────────────
enum class ExpressionValueKind : std::uint8_t {
    Number = 0,
    Bool = 1,
    String = 2,
    Vec2 = 3,
    Vec3 = 4,
    Vec4 = 5,
    Color = 6,
    LayerReference = 7,
    PropertyReference = 8,
};

[[nodiscard]] inline ExpressionValueKind kind(const ExpressionValue& v) noexcept {
    return static_cast<ExpressionValueKind>(v.index());
}

// ── Coercion helpers (return std::nullopt for non-matching type) ─────
[[nodiscard]] inline const double*          as_number(const ExpressionValue& v) noexcept { return std::get_if<double>(&v); }
[[nodiscard]] inline const bool*            as_bool  (const ExpressionValue& v) noexcept { return std::get_if<bool>  (&v); }
[[nodiscard]] inline const std::string*     as_string(const ExpressionValue& v) noexcept { return std::get_if<std::string>(&v); }
[[nodiscard]] inline const Vec2*            as_vec2  (const ExpressionValue& v) noexcept { return std::get_if<Vec2>(&v); }
[[nodiscard]] inline const Vec3*            as_vec3  (const ExpressionValue& v) noexcept { return std::get_if<Vec3>(&v); }
[[nodiscard]] inline const Vec4*            as_vec4  (const ExpressionValue& v) noexcept { return std::get_if<Vec4>(&v); }
[[nodiscard]] inline const Color*           as_color (const ExpressionValue& v) noexcept { return std::get_if<Color>(&v); }
[[nodiscard]] inline const LayerReference*  as_layer (const ExpressionValue& v) noexcept { return std::get_if<LayerReference>(&v); }
[[nodiscard]] inline const PropertyReference* as_prop(const ExpressionValue& v) noexcept { return std::get_if<PropertyReference>(&v); }

// ── Constructors for common initialisations (no std::visit in hot paths) ─
[[nodiscard]] inline ExpressionValue make_number(double d)            noexcept { return d; }
[[nodiscard]] inline ExpressionValue make_bool  (bool   b)            noexcept { return b; }
[[nodiscard]] inline ExpressionValue make_string(std::string s)       noexcept { return std::move(s); }
[[nodiscard]] inline ExpressionValue make_vec2  (Vec2   v)            noexcept { return v; }
[[nodiscard]] inline ExpressionValue make_vec3  (Vec3   v)            noexcept { return v; }
[[nodiscard]] inline ExpressionValue make_vec4  (Vec4   v)            noexcept { return v; }
[[nodiscard]] inline ExpressionValue make_color (Color  c)            noexcept { return c; }
[[nodiscard]] inline ExpressionValue make_layer (LayerReference r)    noexcept { return r; }
[[nodiscard]] inline ExpressionValue make_property(PropertyReference p) noexcept { return p; }

// ── Diagnostic: render an ExpressionValue as a string for debug logs. ──
[[nodiscard]] std::string to_string(const ExpressionValue& v);

} // namespace chronon3d::expressions::v2
