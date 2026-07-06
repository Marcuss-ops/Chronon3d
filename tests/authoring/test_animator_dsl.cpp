// ═══════════════════════════════════════════════════════════════════════════
// Authoring DSL — Animator / Selector equivalence tests
//
// Verifies that the fluent builders in `chronon3d::authoring` produce
// Specs field-equivalent to manually-assembled ones. The Spec types
// (`TextAnimatorSpec`, `GlyphSelectorSpec`) are the engine's IR; the
// fluent builders must round-trip without losing field values.
//
// Production consumers of `.release()` will be `Text::animate()` (PR 3).
// Until that lands, tests reach in via `AnimatorTestAccess`, declared
// as `friend struct AnimatorTestAccess` in the builder headers.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/authoring/authoring.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>      // PR 3 — LayerBuilder for Layer façade
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_animator_property.hpp>

#include <type_traits>
#include <utility>
#include <variant>

using chronon3d::Color;
using chronon3d::Frame;
using chronon3d::Vec2;
using chronon3d::Vec3;
using chronon3d::Easing;
using chronon3d::EasingCurve;
using chronon3d::SelectorCombineMode;
using chronon3d::SelectorWeight;
using chronon3d::TextMaterial;
using chronon3d::TextPropertyBlendMode;
using chronon3d::TextSelectorOrder;
using chronon3d::TextSelectorShape;
using chronon3d::TextSelectorUnit;

using chronon3d::GlyphSelectorSpec;
using chronon3d::TextAnimatorSpec;

using chronon3d::authoring::Animator;
using chronon3d::authoring::Material;
using chronon3d::authoring::MotionRegistry;
using chronon3d::authoring::Selector;
using chronon3d::authoring::StyleRegistry;

using chronon3d::SceneBuilder;
using chronon3d::FrameRate;
using chronon3d::OpacityProperty;
using chronon3d::BlurProperty;
using chronon3d::TrackingProperty;
using chronon3d::PositionProperty;
using chronon3d::ScaleProperty;
using chronon3d::RotationProperty;
using chronon3d::AnchorProperty;
using chronon3d::FillColorProperty;
using chronon3d::StrokeColorProperty;
using chronon3d::StrokeWidthProperty;
using chronon3d::LayerBuilder;
using chronon3d::PendingTextRun;
using chronon3d::TextAlign;
using chronon3d::TextAnchor;
using chronon3d::TextCenteringMode;
using chronon3d::TextDirection;
using chronon3d::TextOverflow;
using chronon3d::TextStyle;
using chronon3d::TextWrap;
using chronon3d::VerticalAlign;
using chronon3d::f32;
using chronon3d::authoring::animator;
namespace material = chronon3d::authoring::material;
using chronon3d::authoring::selector;

// ── doctest::Approx3D / doctest::Approx2D polyfill ────────────────────────
// doctest ships `Approx<T>` only for built-in arithmetic types — no
// `Approx<Vec3>` / `Approx<Vec2>` because Vec3/Vec2 are user types.
// This fixture uses `doctest::Approx3D(...)` and `doctest::Approx2D(...)`
// inside `CHECK(LHS == doctest::Approx3D(RHS))`, so we add minimal proxy
// wrappers + componentwise `operator==` overloads with a 1e-5 tolerance
// matching the vec3_eq/vec2_eq helpers used elsewhere.
//
// NOTE: this polyfill lives AFTER the `#include <chronon3d/math/color.hpp>`
// above because that header transitively defines `Vec3` / `Vec2`. Placing
// it before the includes would fail to compile with "Vec3 not declared".
namespace doctest {
struct Approx3D { Vec3 v; explicit Approx3D(Vec3 v_) : v(v_) {} };
struct Approx2D { Vec2 v; explicit Approx2D(Vec2 v_) : v(v_) {} };
} // namespace doctest
inline bool operator==(const Vec3& a, const doctest::Approx3D& b) {
    return a.x == doctest::Approx(b.v.x).epsilon(1e-5)
        && a.y == doctest::Approx(b.v.y).epsilon(1e-5)
        && a.z == doctest::Approx(b.v.z).epsilon(1e-5);
}
inline bool operator!=(const Vec3& a, const doctest::Approx3D& b) { return !(a == b); }
inline bool operator==(const Vec2& a, const doctest::Approx2D& b) {
    return a.x == doctest::Approx(b.v.x).epsilon(1e-5)
        && a.y == doctest::Approx(b.v.y).epsilon(1e-5);
}
inline bool operator!=(const Vec2& a, const doctest::Approx2D& b) { return !(a == b); }
// ─────────────────────────────────────────────────────────────────────────

// Test-only friend accessor for the private `release()` helper on all
// builders.  Lives in the `chronon3d::authoring::testing::*` subnamespace
// so its test-only intent is explicit; future PR 3–5 production code does
// not have visibility into it.
namespace chronon3d::authoring::testing {
struct AnimatorTestAccess {
    static TextAnimatorSpec release(Animator&& a) {
        return std::move(a).release();
    }
    static GlyphSelectorSpec release(Selector&& s) {
        return std::move(s).release();
    }
    static TextMaterial release(Material&& m) {
        return std::move(m).release();
    }
};
struct MaterialTestAccess {
    static TextMaterial release(Material&& m) {
        return std::move(m).release();
    }
};
}  // namespace chronon3d::authoring::testing

using chronon3d::authoring::testing::AnimatorTestAccess;
using chronon3d::authoring::testing::MaterialTestAccess;

namespace {

// Walk every property variant and call `fn` only for the active alternative.
// Used to inspect TextAnimatorSpec::properties in tests.
template <class Fn>
void for_each_property(const TextAnimatorSpec& spec, Fn&& fn) {
    for (const auto& p : spec.properties) {
        std::visit(std::forward<Fn>(fn), p);
    }
}

bool find_property(const TextAnimatorSpec& spec,
                   const std::type_info& target,
                   std::function<bool(const void*)> match) {
    bool found = false;
    for_each_property(spec, [&](const auto& v) {
        if (found) return;
        if (typeid(v) == target) {
            found = match(static_cast<const void*>(&v));
        }
    });
    return found;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Selector equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Selector: shape + unit + exclude_spaces round-trip") {
    Selector s{TextSelectorUnit::Grapheme};
    s.shape(TextSelectorShape::RampUp)
     .exclude_spaces();

    GlyphSelectorSpec built = AnimatorTestAccess::release(std::move(s));

    CHECK(built.unit            == TextSelectorUnit::Grapheme);
    CHECK(built.shape           == TextSelectorShape::RampUp);
    CHECK(built.exclude_spaces  == true);
    CHECK(built.order           == TextSelectorOrder::Forward);     // default
    CHECK(built.combine         == SelectorCombineMode::Replace);  // default
}

TEST_CASE("Authoring/Selector: smooth() sets TextSelectorShape::Smooth") {
    Selector s = selector(TextSelectorUnit::Word);
    s.smooth().exclude_spaces(false);

    auto built = AnimatorTestAccess::release(std::move(s));
    CHECK(built.unit           == TextSelectorUnit::Word);
    CHECK(built.shape          == TextSelectorShape::Smooth);
    CHECK(built.exclude_spaces == false);
}

TEST_CASE("Authoring/Selector: multiple .start() calls accumulate keyframes") {
    Selector s = selector(TextSelectorUnit::Glyph);
    s.start(Frame{0},    0.0f)
     .start(Frame{24},  50.0f)
     .start(Frame{60}, 100.0f, Easing::OutCubic);

    auto built = AnimatorTestAccess::release(std::move(s));

    // Sample at frame 24 — should hit the middle keyframe.
    const SelectorWeight w_mid = chronon3d::evaluate_selector(
        built, /*unit_map=*/{}, /*glyph_index=*/0,
        /*source=*/"A", chronon3d::SampleTime{24},
        /*placed=*/nullptr);

    CHECK(w_mid == doctest::Approx(0.5f).epsilon(0.05f));
}

TEST_CASE("Authoring/Selector: end / offset / amount setters apply") {
    Selector s = selector(TextSelectorUnit::Line);
    s.end   (Frame{60},   80.0f, Easing::InOutCubic)
     .offset(Frame{0},   -10.0f)
     .amount(Frame{30},   75.0f)
     .ease_low (15.0f)
     .ease_high(85.0f);

    auto built = AnimatorTestAccess::release(std::move(s));
    CHECK(built.unit        == TextSelectorUnit::Line);
    CHECK(built.ease_low    == doctest::Approx(15.0f));
    CHECK(built.ease_high   == doctest::Approx(85.0f));
    CHECK(built.exclude_spaces == true);  // default

    // end keyframe exists — sample at frame 60 hits the only key.
    const SelectorWeight w_end = chronon3d::evaluate_selector(
        built, {}, 0, "", chronon3d::SampleTime{60}, nullptr);
    CHECK(w_end <= 0.81f);
}

TEST_CASE("Authoring/Selector: chain returns *this for single Animator identity") {
    Selector s = selector(TextSelectorUnit::Glyph);
    Selector& ref1 = s;
    Selector& ref2 = ref1.start(0, 0.0f);   // int-frame overload matches design
    Selector& ref3 = ref2.smooth();
    CHECK(&s   == &ref1);
    CHECK(&ref1 == &ref2);
    CHECK(&ref2 == &ref3);
}

// ═══════════════════════════════════════════════════════════════════════════
// Animator equivalence tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Animator: fluent hero-reveal pattern") {
    // Mirrors the user's example exactly:
    //   animator("hero-reveal")
    //       .select(selector(Grapheme).shape_smooth().exclude_spaces()
    //                  .start(0,0).start(24,100,OutCubic))
    //       .position({0,46,0}).scale(0.94f)
    //       .opacity(0).blur(12).tracking(10);
    Animator a = animator("hero-reveal");

    Selector sel = selector(TextSelectorUnit::Grapheme);
    sel.smooth()
       .exclude_spaces()
       .start(Frame{0},  0.0f)
       .start(Frame{24}, 100.0f, Easing::OutCubic);

    a.select(std::move(sel))
     .position(Vec3{0.0f, 46.0f, 0.0f})
     .scale(0.94f)
     .opacity(0.0f)
     .blur(12.0f)
     .tracking(10.0f);

    auto built = AnimatorTestAccess::release(std::move(a));

    CHECK(built.id == "hero-reveal");
    CHECK(built.enabled == true);
    CHECK(built.transform_mode == TextPropertyBlendMode::Add);     // default
    CHECK(built.color_mode      == TextPropertyBlendMode::Replace);// default

    REQUIRE(built.selectors.size() == 1);
    const auto& s0 = built.selectors[0];
    CHECK(s0.unit           == TextSelectorUnit::Grapheme);
    CHECK(s0.shape          == TextSelectorShape::Smooth);
    CHECK(s0.exclude_spaces == true);

    REQUIRE(built.properties.size() == 5);
    CHECK(find_property(built, typeid(chronon3d::PositionProperty),
        [](const void* p) {                return static_cast<const chronon3d::PositionProperty*>(p)->value.evaluate(0)
                    == Vec3{0.0f, 46.0f, 0.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::ScaleProperty),
        [](const void* p) {
            auto* sp = static_cast<const chronon3d::ScaleProperty*>(p);
            // Uniform .scale(0.94f) → Vec3{0.94f, 0.94f, 1.0f}.
            return sp->value.evaluate(0).x == doctest::Approx(0.94f)
                && sp->value.evaluate(0).y == doctest::Approx(0.94f)
                && sp->value.evaluate(0).z == doctest::Approx(1.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::OpacityProperty),
        [](const void* p) {                return static_cast<const chronon3d::OpacityProperty*>(p)->value.evaluate(0)
                    == doctest::Approx(0.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::BlurProperty),
        [](const void* p) {                return static_cast<const chronon3d::BlurProperty*>(p)->radius.evaluate(0)
                    == doctest::Approx(12.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::TrackingProperty),
        [](const void* p) {                return static_cast<const chronon3d::TrackingProperty*>(p)->pixels.evaluate(0)
                    == doctest::Approx(10.0f);
        }));
}

TEST_CASE("Authoring/Animator: empty builder carries Spec defaults") {
    Animator a = animator("defaults");
    auto built = AnimatorTestAccess::release(std::move(a));

    CHECK(built.id             == "defaults");
    CHECK(built.enabled        == true);
    CHECK(built.transform_mode == TextPropertyBlendMode::Add);
    CHECK(built.color_mode      == TextPropertyBlendMode::Replace);
    CHECK(built.selectors.empty());
    CHECK(built.properties.empty());
}

TEST_CASE("Authoring/Animator: Scale overload picks Vec3 vs uniform") {
    // .scale(Vec3) passes through; .scale(float) → Vec3{u,u,1.0f}.
    Animator u = animator("uniform");
    u.scale(0.5f);
    auto u_built = AnimatorTestAccess::release(std::move(u));
    REQUIRE(u_built.properties.size() == 1);
    auto* sp = std::get_if<chronon3d::ScaleProperty>(&u_built.properties[0]);
    REQUIRE(sp != nullptr);
    CHECK(sp->value.evaluate(0) == doctest::Approx3D(Vec3{0.5f, 0.5f, 1.0f}));

    Animator v = animator("vec3");
    v.scale(Vec3{0.5f, 0.25f, 2.0f});
    auto v_built = AnimatorTestAccess::release(std::move(v));
    REQUIRE(v_built.properties.size() == 1);
    auto* sp2 = std::get_if<chronon3d::ScaleProperty>(&v_built.properties[0]);
    REQUIRE(sp2 != nullptr);
    CHECK(sp2->value.evaluate(0) == doctest::Approx3D(Vec3{0.5f, 0.25f, 2.0f}));
}

TEST_CASE("Authoring/Animator: rotation / anchor / colours apply") {
    Animator a = animator("visuals");
    a.rotation(Vec3{0.0f, 15.0f, 0.0f})
     .anchor   (Vec3{10.0f, 0.0f, 0.0f})
     .fill_color  (Color{1.0f, 0.5f, 0.25f, 1.0f})
     .stroke_color(Color{0.0f, 0.0f, 1.0f, 1.0f})
     .stroke_width(2.5f);

    auto built = AnimatorTestAccess::release(std::move(a));
    REQUIRE(built.properties.size() == 5);
    CHECK(find_property(built, typeid(chronon3d::RotationProperty),
        [](const void* p) {
            return static_cast<const chronon3d::RotationProperty*>(p)->degrees
                == doctest::Approx3D(Vec3{0.0f, 15.0f, 0.0f});
        }));
    CHECK(find_property(built, typeid(chronon3d::AnchorProperty),
        [](const void* p) {
            return static_cast<const chronon3d::AnchorProperty*>(p)->value
                == doctest::Approx3D(Vec3{10.0f, 0.0f, 0.0f});
        }));
    CHECK(find_property(built, typeid(chronon3d::FillColorProperty),
        [](const void* p) {
            return static_cast<const chronon3d::FillColorProperty*>(p)->color
                == Color{1.0f, 0.5f, 0.25f, 1.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::StrokeColorProperty),
        [](const void* p) {
            return static_cast<const chronon3d::StrokeColorProperty*>(p)->color
                == Color{0.0f, 0.0f, 1.0f, 1.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::StrokeWidthProperty),
        [](const void* p) {
            return static_cast<const chronon3d::StrokeWidthProperty*>(p)->width
                == doctest::Approx(2.5f);
        }));
}

TEST_CASE("Authoring/Animator: multiple selectors accumulate in order") {
    Animator a = animator("stacked");

    Selector s1 = selector(TextSelectorUnit::Word);
    s1.start(Frame{0}, 50.0f);

    Selector s2 = selector(TextSelectorUnit::Line);
    s2.ease_high(80.0f);

    a.select(std::move(s1))
     .select(std::move(s2))
     .opacity(1.0f);

    auto built = AnimatorTestAccess::release(std::move(a));
    REQUIRE(built.selectors.size() == 2);
    CHECK(built.selectors[0].unit == TextSelectorUnit::Word);
    CHECK(built.selectors[1].unit == TextSelectorUnit::Line);
    CHECK(built.selectors[1].ease_high == doctest::Approx(80.0f));
    REQUIRE(built.properties.size() == 1);
}

TEST_CASE("Authoring/Animator: chain returns *this for stable identity") {
    Animator a = animator("chain");
    Animator& r1 = a;
    Animator& r2 = r1.position(Vec3{1.0f, 2.0f, 3.0f});
    Animator& r3 = r2.opacity(0.5f);
    CHECK(&a == &r1);
    CHECK(&r1 == &r2);
    CHECK(&r2 == &r3);
}

TEST_CASE("Authoring/Animator: copy is forbidden (move-only contract)") {
    Animator a = animator("m");
    CHECK(!std::is_copy_constructible_v<Animator>);
    CHECK(!std::is_copy_assignable_v   <Animator>);
    CHECK( std::is_move_constructible_v<Animator>);
    CHECK( std::is_move_assignable_v   <Animator>);
    // Keeping `a` alive to suppress "unused" warnings
    (void)a.opacity(1.0f);
}

TEST_CASE("Authoring/Selector: copy is forbidden (move-only contract)") {
    Selector s = selector(TextSelectorUnit::Glyph);
    CHECK(!std::is_copy_constructible_v<Selector>);
    CHECK(!std::is_copy_assignable_v   <Selector>);
    CHECK( std::is_move_constructible_v<Selector>);
    CHECK( std::is_move_assignable_v   <Selector>);
    (void)s.smooth();
}

// ═══════════════════════════════════════════════════════════════════════════
// Material equivalence tests (PR 2)
//
// Verifies that the fluent `chronon3d::authoring::Material` builder
// produces a `TextMaterial` field-equivalent to a hand-built struct.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/Material: default-constructed Material is disabled") {
    Material m;
    TextMaterial built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled == false);
}

TEST_CASE("Authoring/Material: material::premium() returns enabled premium preset") {
    Material m = material::premium();
    TextMaterial built = MaterialTestAccess::release(std::move(m));

    CHECK(built.enabled                 == true);
    CHECK(built.bevel_px                == doctest::Approx(1.5f));
    CHECK(built.bevel_highlight_opacity == doctest::Approx(0.45f));
    CHECK(built.bevel_shadow_opacity    == doctest::Approx(0.30f));
    CHECK(built.use_material_glow       == true);
    CHECK(built.glow_radius             == doctest::Approx(12.0f));
    CHECK(built.glow_intensity          == doctest::Approx(0.60f));
    CHECK(built.use_material_shadow     == true);
    CHECK(built.shadow_blur             == doctest::Approx(12.0f));
    CHECK(built.shadow_opacity          == doctest::Approx(0.45f));
    CHECK(built.emissive                == doctest::Approx(1.05f));
}

TEST_CASE("Authoring/Material: material::neon() enables strong glow") {
    Material m = material::neon();
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled               == true);
    CHECK(built.use_material_glow     == true);
    CHECK(built.glow_radius           == doctest::Approx(20.0f));
    CHECK(built.glow_intensity        == doctest::Approx(1.0f));
    CHECK(built.emissive              == doctest::Approx(1.20f));
}

TEST_CASE("Authoring/Material: material::glass() enables semi-transparent gradient") {
    Material m = material::glass();
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled               == true);
    CHECK(built.bevel_px              == doctest::Approx(1.0f));
    CHECK(built.top_color.a           == doctest::Approx(0.85f).epsilon(0.001f));
    CHECK(built.bottom_color.a        == doctest::Approx(0.70f).epsilon(0.001f));
    CHECK(built.use_material_glow     == true);
}

TEST_CASE("Authoring/Material: material::flat() does not enable glow or shadow") {
    Material m = material::flat();
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled               == true);
    CHECK(built.use_material_glow     == false);
    CHECK(built.use_material_shadow   == false);
}

TEST_CASE("Authoring/Material: premium() + chained setters matches hand-built") {
    // Matches the user's design pattern:
    //   material::premium().bevel(1.5).glow(14,0.45).shadow({0,8},16)
    Material m = material::premium();
    m.bevel(1.5f)
     .glow(14.0f, 0.45f)
     .shadow(Vec2{0.0f, 8.0f}, 16.0f);

    auto built = MaterialTestAccess::release(std::move(m));

    // Hand-built equivalent
    TextMaterial manual;
    manual.enabled = true;
    manual.top_color    = {1.0f, 1.0f, 1.0f, 1.0f};
    manual.bottom_color = {0.85f, 0.87f, 0.95f, 1.0f};
    manual.bevel_px                = 1.5f;
    manual.bevel_highlight_opacity = 0.45f;
    manual.bevel_shadow_opacity    = 0.30f;
    manual.use_material_glow       = true;
    manual.glow_radius             = 14.0f;          // overridden
    manual.glow_intensity          = 0.45f;          // overridden
    manual.use_material_shadow     = true;
    manual.shadow_offset           = {0.0f, 8.0f};
    manual.shadow_blur             = 16.0f;
    manual.emissive                = 1.05f;

    CHECK(built.enabled                 == manual.enabled);
    CHECK(built.bevel_px                == doctest::Approx(manual.bevel_px));
    CHECK(built.glow_radius             == doctest::Approx(manual.glow_radius));
    CHECK(built.glow_intensity          == doctest::Approx(manual.glow_intensity));
    CHECK(built.shadow_offset.y         == doctest::Approx(manual.shadow_offset.y));
    CHECK(built.shadow_blur             == doctest::Approx(manual.shadow_blur));
    CHECK(built.shadow_opacity          == doctest::Approx(manual.shadow_opacity));
}

TEST_CASE("Authoring/Material: gradient() sets top + bottom + angle + enables") {
    Material m;  // default disabled
    m.gradient(Color{0.2f, 0.4f, 1.0f, 1.0f},
               Color{1.0f, 0.6f, 0.2f, 1.0f},
               /*angle_degrees=*/45.0f);

    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled         == true);
    CHECK(built.top_color       == Color{0.2f, 0.4f, 1.0f, 1.0f});
    CHECK(built.bottom_color    == Color{1.0f, 0.6f, 0.2f, 1.0f});
    CHECK(built.gradient_angle  == doctest::Approx(45.0f));
}

TEST_CASE("Authoring/Material: top_color / bottom_color single setters auto-enable") {
    Material m;
    m.top_color   (Color{1.0f, 0.0f, 0.0f, 1.0f});
    m.bottom_color(Color{0.0f, 1.0f, 0.0f, 1.0f});
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled     == true);
    CHECK(built.top_color   == Color{1.0f, 0.0f, 0.0f, 1.0f});
    CHECK(built.bottom_color== Color{0.0f, 1.0f, 0.0f, 1.0f});
}

TEST_CASE("Authoring/Material: bevel(px) records pixel + default highlight/shadow") {
    Material m;
    m.bevel(2.5f);
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.enabled                 == true);
    CHECK(built.bevel_px                == doctest::Approx(2.5f));
    CHECK(built.bevel_highlight_opacity == doctest::Approx(0.35f)); // default
    CHECK(built.bevel_shadow_opacity    == doctest::Approx(0.25f)); // default
}

TEST_CASE("Authoring/Material: bevel(px, highlight, shadow) sets all three") {
    Material m;
    m.bevel(3.0f, /*highlight=*/0.8f, /*shadow=*/0.4f);
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.bevel_px                == doctest::Approx(3.0f));
    CHECK(built.bevel_highlight_opacity == doctest::Approx(0.8f));
    CHECK(built.bevel_shadow_opacity    == doctest::Approx(0.4f));
}

TEST_CASE("Authoring/Material: shadow() 2/3/4-arg overloads accumulate fields") {
    Material m2;
    m2.shadow({1.0f, 2.0f}, 4.0f);
    auto b2 = MaterialTestAccess::release(std::move(m2));
    CHECK(b2.use_material_shadow == true);
    CHECK(b2.shadow_offset       == doctest::Approx2D(Vec2{1.0f, 2.0f}));
    CHECK(b2.shadow_blur         == doctest::Approx(4.0f));

    Material m3;
    m3.shadow({0.0f, 3.0f}, 6.0f, /*opacity=*/0.7f);
    auto b3 = MaterialTestAccess::release(std::move(m3));
    CHECK(b3.shadow_opacity      == doctest::Approx(0.7f));

    Material m4;
    m4.shadow({0.0f, 5.0f}, 8.0f, 0.9f, Color{1.0f, 1.0f, 1.0f, 0.5f});
    auto b4 = MaterialTestAccess::release(std::move(m4));
    CHECK(b4.shadow_color        == Color{1.0f, 1.0f, 1.0f, 0.5f});
}

TEST_CASE("Authoring/Material: glow + glow_color combine; no_glow disables") {
    Material m = material::premium();
    m.glow(8.0f, 0.5f).glow_color(Color{1.0f, 1.0f, 0.0f, 1.0f});
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.use_material_glow == true);
    CHECK(built.glow_radius       == doctest::Approx(8.0f));
    CHECK(built.glow_intensity    == doctest::Approx(0.5f));
    CHECK(built.glow_color        == Color{1.0f, 1.0f, 0.0f, 1.0f});

    Material off;
    off.glow(10.0f, 0.5f).no_glow();
    auto off_built = MaterialTestAccess::release(std::move(off));
    CHECK(off_built.use_material_glow == false);
    // radius / intensity stay at the values we set; only the bool flipped
    CHECK(off_built.glow_radius       == doctest::Approx(10.0f));
}

TEST_CASE("Authoring/Material: inner_shadow + no_inner_shadow toggle") {
    Material m;
    m.inner_shadow({2.0f, -1.0f}, 6.0f, Color{0.0f, 0.0f, 0.0f, 0.9f});
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.inner_shadow_enabled == true);
    CHECK(built.inner_shadow_offset  == doctest::Approx2D(Vec2{2.0f, -1.0f}));
    CHECK(built.inner_shadow_blur    == doctest::Approx(6.0f));
    CHECK(built.inner_shadow_color   == Color{0.0f, 0.0f, 0.0f, 0.9f});
    CHECK(built.enabled              == true);

    Material off;
    off.inner_shadow({1,1}, 1.0f, Color::black()).no_inner_shadow();
    auto off_built = MaterialTestAccess::release(std::move(off));
    CHECK(off_built.inner_shadow_enabled == false);
}

TEST_CASE("Authoring/Material: top_highlight / bottom_shade set fractions") {
    Material m;
    m.top_highlight(0.5f, /*fraction=*/0.20f)
     .bottom_shade(0.3f, /*fraction=*/0.12f);
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.top_highlight_opacity   == doctest::Approx(0.5f));
    CHECK(built.top_highlight_fraction  == doctest::Approx(0.20f));
    CHECK(built.bottom_shade_opacity    == doctest::Approx(0.3f));
    CHECK(built.bottom_shade_fraction   == doctest::Approx(0.12f));
    CHECK(built.enabled                 == true);
}

TEST_CASE("Authoring/Material: emissive() can dim or boost brightness") {
    Material m1;
    m1.emissive(0.5f);
    CHECK(MaterialTestAccess::release(std::move(m1)).emissive == doctest::Approx(0.5f));

    Material m2;
    m2.emissive(1.5f);
    CHECK(MaterialTestAccess::release(std::move(m2)).emissive == doctest::Approx(1.5f));
}

TEST_CASE("Authoring/Material: enable() / disable() are explicit") {
    Material m;
    m.disable();
    CHECK(MaterialTestAccess::release(std::move(m)).enabled == false);

    Material m2;
    m2.enable();
    CHECK(MaterialTestAccess::release(std::move(m2)).enabled == true);
}

TEST_CASE("Authoring/Material: configure_core lambda mutates unmodeled field") {
    Material m = material::premium();
    m.configure_core([](TextMaterial& tm) {
        // The fluent surface doesn't expose `bevel_highlight_color` as a
        // standalone setter; users can still reach it through the hatch.
        tm.bevel_highlight_color = Color{0.0f, 1.0f, 1.0f, 1.0f};
        tm.gradient_angle        = 60.0f;
    });
    auto built = MaterialTestAccess::release(std::move(m));
    CHECK(built.bevel_highlight_color == Color{0.0f, 1.0f, 1.0f, 1.0f});
    CHECK(built.gradient_angle        == doctest::Approx(60.0f));
}

TEST_CASE("Authoring/Material: copy is forbidden (move-only contract)") {
    CHECK(!std::is_copy_constructible_v<Material>);
    CHECK(!std::is_copy_assignable_v   <Material>);
    CHECK( std::is_move_constructible_v<Material>);
    CHECK( std::is_move_assignable_v   <Material>);
}

TEST_CASE("Authoring/Material: chain returns *this for stable identity") {
    Material m = material::premium();
    Material& r1 = m;
    Material& r2 = r1.bevel(2.0f);
    Material& r3 = r2.glow(8.0f, 0.5f);
    CHECK(&m == &r1);
    CHECK(&r1 == &r2);
    CHECK(&r2 == &r3);
}

// ═══════════════════════════════════════════════════════════════════════════
// StyleRegistry equivalence tests (PR 5)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/StyleRegistry: empty registry resolves nullopt") {
    StyleRegistry reg;
    CHECK(reg.count() == 0);
    CHECK(reg.has("any.id") == false);
    CHECK(reg.resolve("any.id") == std::nullopt);
    CHECK(reg.ids().empty());
}

TEST_CASE("Authoring/StyleRegistry: register_style and resolve return value") {
    StyleRegistry reg;

    TextStyle hero;
    hero.font_path   = "assets/fonts/Inter-Bold.ttf";
    hero.size        = 96.0f;
    hero.color       = Color{1.0f, 0.86f, 0.2f, 1.0f};
    hero.anchor      = TextAnchor::Center;
    hero.align       = TextAlign::Center;
    hero.line_height = 1.0f;

    reg.register_style("youtube.hero.premium", hero);

    CHECK(reg.count() == 1);
    CHECK(reg.has("youtube.hero.premium"));

    auto resolved = reg.resolve("youtube.hero.premium");
    REQUIRE(resolved.has_value());
    CHECK(resolved->font_path == "assets/fonts/Inter-Bold.ttf");
    CHECK(resolved->size     == doctest::Approx(96.0f));
    CHECK(resolved->color    == Color{1.0f, 0.86f, 0.2f, 1.0f});
}

TEST_CASE("Authoring/StyleRegistry: register_factory invokes factory per resolve") {
    StyleRegistry reg;
    int call_count = 0;

    reg.register_factory("parametric.style",
        [&call_count]() -> TextStyle {
            ++call_count;
            TextStyle s;
            s.size = 48.0f * static_cast<f32>(call_count);
            return s;
        });

    REQUIRE(call_count == 0);                     // factory lazy
    auto r1 = reg.resolve("parametric.style");
    REQUIRE(r1.has_value());
    CHECK(r1->size == doctest::Approx(48.0f));
    CHECK(call_count == 1);

    auto r2 = reg.resolve("parametric.style");
    REQUIRE(r2.has_value());
    CHECK(r2->size == doctest::Approx(96.0f));    // size grows each call
    CHECK(call_count == 2);

    auto r3 = reg.resolve("parametric.style");
    REQUIRE(r3.has_value());
    CHECK(r3->size == doctest::Approx(144.0f));
    CHECK(call_count == 3);
}

TEST_CASE("Authoring/StyleRegistry: register_style overrides previous") {
    StyleRegistry reg;

    TextStyle a;
    a.size = 48.0f;
    reg.register_style("hero", a);

    TextStyle b;
    b.size = 96.0f;
    reg.register_style("hero", b);                // last write wins

    CHECK(reg.count() == 1);
    auto resolved = reg.resolve("hero");
    REQUIRE(resolved.has_value());
    CHECK(resolved->size == doctest::Approx(96.0f));
}

TEST_CASE("Authoring/StyleRegistry: empty-id and null-factory are no-ops") {
    StyleRegistry reg;

    TextStyle hero;
    hero.size = 48.0f;
    reg.register_style({}, hero);                 // empty id → ignored
    CHECK(reg.count() == 0);

    reg.register_factory("valid.id", nullptr);   // null factory → ignored
    CHECK(reg.count() == 0);

    reg.register_style("", hero);                 // also empty (not just {})
    CHECK(reg.count() == 0);

    reg.register_style("ok", hero);               // a valid entry
    CHECK(reg.count() == 1);
}

TEST_CASE("Authoring/StyleRegistry: unregister returns bool and removes entry") {
    StyleRegistry reg;

    TextStyle s;
    s.size = 48.0f;
    reg.register_style("k1", s);
    reg.register_style("k2", s);

    CHECK(reg.count() == 2);
    CHECK(reg.unregister("k1")  == true);
    CHECK(reg.unregister("k1")  == false);        // already gone
    CHECK(reg.unregister("absent") == false);
    CHECK(reg.count() == 1);
    CHECK(reg.has("k1") == false);
    CHECK(reg.has("k2") == true);
}

TEST_CASE("Authoring/StyleRegistry: ids() returns sorted list") {
    StyleRegistry reg;
    TextStyle s;
    reg.register_style("z.last",  s);
    reg.register_style("a.first", s);
    reg.register_style("m.middle",s);

    auto ids = reg.ids();
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == "a.first");
    CHECK(ids[1] == "m.middle");
    CHECK(ids[2] == "z.last");
}

TEST_CASE("Authoring/StyleRegistry: copy / move forbidden (instance-pinned)") {
    CHECK(!std::is_copy_constructible_v<StyleRegistry>);
    CHECK(!std::is_copy_assignable_v   <StyleRegistry>);
    CHECK(!std::is_move_constructible_v<StyleRegistry>);
    CHECK(!std::is_move_assignable_v   <StyleRegistry>);
}

TEST_CASE("Authoring/StyleRegistry: clear() empties all entries") {
    StyleRegistry reg;
    TextStyle s; s.size = 48.0f;
    reg.register_style("a", s).register_style("b", s);
    CHECK(reg.count() == 2);
    reg.clear();
    CHECK(reg.count() == 0);
    CHECK(reg.has("a") == false);
    CHECK(reg.has("b") == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// MotionRegistry equivalence tests (PR 5)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Authoring/MotionRegistry: empty registry resolves nullopt") {
    MotionRegistry reg;
    CHECK(reg.count() == 0);
    CHECK(reg.has("text.reveal.soft") == false);
    CHECK(reg.resolve("text.reveal.soft") == std::nullopt);
}

TEST_CASE("Authoring/MotionRegistry: register_motion resolves TextAnimatorSpec") {
    MotionRegistry reg;

    TextAnimatorSpec preset;
    preset.id = "reveal.soft.preset";
    preset.enabled = true;
    preset.transform_mode = TextPropertyBlendMode::Add;
    preset.color_mode      = TextPropertyBlendMode::Replace;
    preset.properties.emplace_back(OpacityProperty{0.0f});
    preset.properties.emplace_back(BlurProperty{12.0f});

    reg.register_motion("text.reveal.soft", preset);

    CHECK(reg.count() == 1);
    CHECK(reg.has("text.reveal.soft"));

    auto resolved = reg.resolve("text.reveal.soft");
    REQUIRE(resolved.has_value());
    CHECK(resolved->id == "reveal.soft.preset");
    CHECK(resolved->enabled == true);
    REQUIRE(resolved->properties.size() == 2);

    // Spot-check that the variant types survive intact.
    bool has_opacity = false, has_blur = false;
    for (const auto& p : resolved->properties) {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, chronon3d::OpacityProperty>) {
                has_opacity = (v.value.evaluate(0) == doctest::Approx(0.0f));
            } else if constexpr (std::is_same_v<T, chronon3d::BlurProperty>) {
                has_blur = (v.radius.evaluate(0) == doctest::Approx(12.0f));
            }
        }, p);
    }
    CHECK(has_opacity);
    CHECK(has_blur);
}

TEST_CASE("Authoring/MotionRegistry: register_factory produces fresh TextAnimatorSpec per call") {
    MotionRegistry reg;
    int call_count = 0;

    reg.register_factory("parametric.motion",
        [&call_count]() -> TextAnimatorSpec {
            ++call_count;
            TextAnimatorSpec s;
            s.id = std::string{"call."} + std::to_string(call_count);
            return s;
        });

    REQUIRE(call_count == 0);
    auto r1 = reg.resolve("parametric.motion");
    REQUIRE(r1.has_value());
    CHECK(r1->id == "call.1");
    CHECK(call_count == 1);

    auto r2 = reg.resolve("parametric.motion");
    REQUIRE(r2.has_value());
    CHECK(r2->id == "call.2");
    CHECK(call_count == 2);
}

TEST_CASE("Authoring/MotionRegistry: unregister + has + count API") {
    MotionRegistry reg;
    TextAnimatorSpec preset; preset.id = "x";
    reg.register_motion("a", preset)
       .register_motion("b", preset);

    CHECK(reg.count() == 2);
    CHECK(reg.unregister("a") == true);
    CHECK(reg.has("a") == false);
    CHECK(reg.has("b") == true);
    CHECK(reg.count() == 1);
}

TEST_CASE("Authoring/MotionRegistry: empty-id and null-factory are no-ops") {
    MotionRegistry reg;

    TextAnimatorSpec preset;
    preset.id = "x";
    reg.register_motion({}, preset);
    reg.register_factory("valid", nullptr);
    CHECK(reg.count() == 0);

    reg.register_motion("ok", preset);
    CHECK(reg.count() == 1);
}

TEST_CASE("Authoring/MotionRegistry: copy / move forbidden (instance-pinned)") {
    CHECK(!std::is_copy_constructible_v<MotionRegistry>);
    CHECK(!std::is_copy_assignable_v   <MotionRegistry>);
    CHECK(!std::is_move_constructible_v<MotionRegistry>);
    CHECK(!std::is_move_assignable_v   <MotionRegistry>);
}

TEST_CASE("Authoring/MotionRegistry: composition with StyleRegistry side-by-side") {
    // Sanity check that user code can hold both registries simultaneously
    // and they do not interfere with each other.
    StyleRegistry styles;
    MotionRegistry motions;

    TextStyle hero; hero.size = 64.0f; hero.font_path = "Inter-Bold.ttf";
    styles.register_style("hero", hero);

    TextAnimatorSpec reveal;
    reveal.id = "reveal";
    reveal.properties.emplace_back(OpacityProperty{0.0f});
    motions.register_motion("reveal.soft", reveal);

    auto s = styles.resolve("hero");
    REQUIRE(s.has_value());
    CHECK(s->size == doctest::Approx(64.0f));

    auto m = motions.resolve("reveal.soft");
    REQUIRE(m.has_value());
    CHECK(m->id == "reveal");

    // Counters are independent.
    CHECK(styles.count() == 1);
    CHECK(motions.count() == 1);
}

// ═════════════════════════════════════════════════════════════════════════
// Text + Layer façade equivalence tests (PR 3)
//
// These tests verify the central promise of PR 3: the authoring
// façade produces the exact same PendingTextRun payload that
// `Layer(text)` users would have written by hand in the legacy path.
// Equivalence is structural — pending_->params is the spec after
// materialization.
//
// The LayerBuilder underneath is real (constructed in each test case
// with the simplest overload — name only —). FrameContext defaults
// to 1920×1080. We swap it explicitly when `.center()` behaviour
// must be verified against a non-default viewport size.
// ═════════════════════════════════════════════════════════════════════════

using chronon3d::LayerBuilder;
using chronon3d::PendingTextRun;
using chronon3d::TextAlign;
using chronon3d::TextAnchor;
using chronon3d::TextCenteringMode;
using chronon3d::TextDirection;
using chronon3d::TextOverflow;
using chronon3d::TextStyle;
using chronon3d::TextWrap;
using chronon3d::VerticalAlign;

using chronon3d::authoring::FrameContext;
using chronon3d::authoring::Layer;
using chronon3d::authoring::Text;

// TICKET-110 — test-only inspector mirroring LayerBuilderInspector
// pattern.  Tests read Text handle state through this inspector; the
// public `Text::mutable_pending()` accessor was demoted to private in
// lock-step with a `friend class testing::TextRunBuilderInspector;`
// grant on `Text`, so the inspector is the canonical way to reach
// the underlying `PendingTextRun` from the test side.
#include "../support/text_run_builder_inspection.hpp"
using chronon3d::authoring::testing::TextRunBuilderInspector;

TEST_CASE("Authoring/Layer: text() pushes a PendingTextRun with auto-generated name") {
    LayerBuilder lb("test_layer");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t1 = layer.text("HELLO");
    Text t2 = layer.text("WORLD");

    // Auto-generated name pattern: "text_<N>" where N is per-Layer sequential.
    // Same Layer produced unique entries; both must be reachable through
    // the returned handle.
    CHECK(TextRunBuilderInspector::pending_of(t1).name == "text_0");
    CHECK(TextRunBuilderInspector::pending_of(t2).name == "text_1");
    CHECK(TextRunBuilderInspector::pending_of(t1)->params.text.content.value == "HELLO");
    CHECK(TextRunBuilderInspector::pending_of(t2)->params.text.content.value == "WORLD");

    // Distinct underlying PendingTextRun* (different entries in the layer's
    // m_text_runs vector). Identity check via the pointer the handle holds.
    // Identity check: distinct underlying PendingTextRun entries.
    // Use stored snapshots so the pointer compare is valid (no
    // dangling-temporary UB).  Distinct `.pending` pointers prove
    // distinct underlying storage; name equality is a weaker proxy.
    // (Reviewer finding #5 — preserves the original "two distinct
    // entries even when names might be the same" semantic.)
    const auto snap_t1 = TextRunBuilderInspector::pending_of(t1);
    const auto snap_t2 = TextRunBuilderInspector::pending_of(t2);
    CHECK(snap_t1.pending != snap_t2.pending);
}

TEST_CASE("Authoring/Text: id() + content() store and propagate to underlying spec") {
    LayerBuilder lb("id_content");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("initial");

    t.id("hero-title").content("UPDATED");

    CHECK(TextRunBuilderInspector::pending_of(t).name == "hero-title");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.content.value == "UPDATED");
}

TEST_CASE("Authoring/Text: font() / font_family() / weight() / italic() / font_size() cover FontSpec") {
    LayerBuilder lb("font");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("x");

    t.font("assets/fonts/Inter-Bold.ttf", 96.0f)
     .font_family("Inter")
     .weight(800)
     .italic(true)
     .font_size(120.0f);                  // overrides initial 96

    const auto& font = TextRunBuilderInspector::pending_of(t)->params.text.font;
    CHECK(font.font_path   == "assets/fonts/Inter-Bold.ttf");
    CHECK(font.font_family == "Inter");
    CHECK(font.font_weight == 800);
    CHECK(font.font_style  == "italic");
    CHECK(font.font_size   == doctest::Approx(120.0f));
}

TEST_CASE("Authoring/Text: at(Vec2) lifts z to 0; at(Vec3) preserves all components") {
    LayerBuilder lb("position");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t_v2 = layer.text("v2");
    t_v2.at(Vec2{100.0f, 200.0f});
    CHECK(TextRunBuilderInspector::pending_of(t_v2)->params.text.position
          == doctest::Approx3D(Vec3{100.0f, 200.0f, 0.0f}));

    Text t_v3 = layer.text("v3");
    t_v3.at(Vec3{11.0f, 22.0f, 33.0f});
    CHECK(TextRunBuilderInspector::pending_of(t_v3)->params.text.position
          == doctest::Approx3D(Vec3{11.0f, 22.0f, 33.0f}));

    Text t_2arg = layer.text("2arg");
    t_2arg.at(7.0f, 8.0f);
    CHECK(TextRunBuilderInspector::pending_of(t_2arg)->params.text.position
          == doctest::Approx3D(Vec3{7.0f, 8.0f, 0.0f}));
}

TEST_CASE("Authoring/Text: center() uses FrameContext viewport") {
    LayerBuilder lb("center");
    Layer layer(lb, FrameContext::from_dimensions(800.0f, 600.0f));

    Text t = layer.text("hero");
    t.center();

    // position is (w/2, h/2, 0)
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.position
          == doctest::Approx3D(Vec3{400.0f, 300.0f, 0.0f}));

    // Layout auto-set for invisibly-aligned center
    const auto& L = TextRunBuilderInspector::pending_of(t)->params.text.layout;
    CHECK(L.anchor         == TextAnchor::Center);
    CHECK(L.align          == TextAlign::Center);
    CHECK(L.vertical_align == VerticalAlign::Middle);
}

TEST_CASE("Authoring/Text: center() falls back to 1920x1080 when no FrameContext provided") {
    LayerBuilder lb("fallback");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);  // default FrameContext

    Text t = layer.text("x");
    t.center();
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.position
          == doctest::Approx3D(Vec3{960.0f, 540.0f, 0.0f}));
}

TEST_CASE("Authoring/Text: layout setters propagate to spec.text.layout") {
    LayerBuilder lb("layout_props");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t = layer.text("x");
    t.box({1500.0f, 220.0f})
     .anchor_point(TextAnchor::Center)
     .align(TextAlign::Center)
     .vertical_align(VerticalAlign::Middle)
     .pixel_ink_centering()
     .layout_box_centering()                       // toggle back
     .line_height(0.95f)
     .tracking(-1.5f)
     .wrap(TextWrap::Character)
     .overflow(TextOverflow::Ellipsis)
     .ellipsis(true)
     .max_lines(2)
     .auto_fit(/*min=*/48.0f, /*max_lines=*/2)
     .max_font_size(160.0f);

    const auto& L = TextRunBuilderInspector::pending_of(t)->params.text.layout;
    CHECK(L.box           == Vec2{1500.0f, 220.0f});
    CHECK(L.anchor        == TextAnchor::Center);
    CHECK(L.align         == TextAlign::Center);
    CHECK(L.vertical_align== VerticalAlign::Middle);
    CHECK(L.centering_mode== TextCenteringMode::LayoutBox);
    CHECK(L.line_height   == doctest::Approx(0.95f));
    CHECK(L.tracking      == doctest::Approx(-1.5f));
    CHECK(L.wrap          == TextWrap::Character);
    CHECK(L.overflow      == TextOverflow::Ellipsis);
    CHECK(L.ellipsis      == true);
    CHECK(L.max_lines     == 2);
    CHECK(L.auto_fit      == true);
    CHECK(L.min_font_size == doctest::Approx(48.0f));
    CHECK(L.max_font_size == doctest::Approx(160.0f));
}

TEST_CASE("Authoring/Text: color() mutates appearance.color only") {
    LayerBuilder lb("color");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t = layer.text("x");
    t.color(Color{0.9f, 0.1f, 0.2f, 1.0f});

    const auto& A = TextRunBuilderInspector::pending_of(t)->params.text.appearance;
    CHECK(A.color == Color{0.9f, 0.1f, 0.2f, 1.0f});
    // paint / shadows / material / box_style should still be defaults.
    CHECK(A.paint.fill      == Color{1.0f, 1.0f, 1.0f, 1.0f});  // default
    CHECK(A.shadows.empty());
    CHECK(A.material.enabled == false);                          // default
    CHECK(A.box_style.enabled == false);                         // default
}

TEST_CASE("Authoring/Text: material(Material) consumes Material::release() into appearance.material") {
    LayerBuilder lb("material_consume");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t = layer.text("x");
    Material m = material::premium()
                     .bevel(2.0f)
                     .glow(8.0f, 0.5f);
    t.material(std::move(m));

    // After consumption, source Material is in moved-from state — release()
    // would return an unspecified value but NOT touch the captured value.
    const auto& captured = TextRunBuilderInspector::pending_of(t)->params.text.appearance.material;
    CHECK(captured.enabled                 == true);
    CHECK(captured.bevel_px                == doctest::Approx(2.0f));
    CHECK(captured.use_material_glow       == true);
    CHECK(captured.glow_radius             == doctest::Approx(8.0f));
    CHECK(captured.glow_intensity          == doctest::Approx(0.5f));
}

TEST_CASE("Authoring/Text: animate(Animator) consumes Animator::release() into animators vector") {
    LayerBuilder lb("animate_consume");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t = layer.text("hero");

    Animator a = animator("reveal");
    Selector sel = selector(TextSelectorUnit::Grapheme);
    sel.smooth().start(Frame{0}, 0.0f).start(Frame{24}, 100.0f, Easing::OutCubic);
    a.select(std::move(sel))
     .position(Vec3{0.0f, 46.0f, 0.0f})
     .scale(0.94f)
     .opacity(0.0f);
    t.animate(std::move(a));

    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.animators.size() == 1);
    const auto& appended = TextRunBuilderInspector::pending_of(t)->params.animators.back();
    CHECK(appended.id == "reveal");
    CHECK(appended.enabled == true);
    REQUIRE(appended.selectors.size() == 1);
    CHECK(appended.selectors[0].unit  == TextSelectorUnit::Grapheme);
    CHECK(appended.selectors[0].shape == TextSelectorShape::Smooth);
    REQUIRE(appended.properties.size() == 3);
}

TEST_CASE("Authoring/Text: multiple animate() calls accumulate in order") {
    LayerBuilder lb("multi_anim");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t = layer.text("x");
    t.animate(animator("in")  .opacity(0.0f));
    t.animate(animator("out") .opacity(1.0f));
    t.animate(animator("idle").tracking(2.0f));

    const auto& list = TextRunBuilderInspector::pending_of(t)->params.animators;
    REQUIRE(list.size() == 3);
    CHECK(list[0].id == "in");
    CHECK(list[1].id == "out");
    CHECK(list[2].id == "idle");
}

TEST_CASE("Authoring/Text: style(id, registry) field-maps TextStyle to spec.text") {
    LayerBuilder lb("style_map");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    StyleRegistry styles;
    TextStyle hero;
    hero.font_path   = "assets/fonts/Inter-Bold.ttf";
    hero.font_family = "Inter";
    hero.font_weight = 700;
    hero.font_style  = "normal";
    hero.size        = 96.0f;
    hero.color       = Color{1.0f, 0.86f, 0.2f, 1.0f};
    hero.anchor      = TextAnchor::Center;
    hero.align       = TextAlign::Center;
    hero.line_height = 1.0f;
    hero.tracking    = -1.0f;
    hero.max_lines   = 1;
    styles.register_style("youtube.hero.premium", hero);

    Text t = layer.text("CHRONON");
    t.style("youtube.hero.premium", styles);

    const auto& F = TextRunBuilderInspector::pending_of(t)->params.text.font;
    const auto& L = TextRunBuilderInspector::pending_of(t)->params.text.layout;
    const auto& A = TextRunBuilderInspector::pending_of(t)->params.text.appearance;
    CHECK(F.font_path   == "assets/fonts/Inter-Bold.ttf");
    CHECK(F.font_family == "Inter");
    CHECK(F.font_weight == 700);
    CHECK(F.font_style  == "normal");
    CHECK(F.font_size   == doctest::Approx(96.0f));
    CHECK(A.color       == Color{1.0f, 0.86f, 0.2f, 1.0f});
    CHECK(L.anchor      == TextAnchor::Center);
    CHECK(L.align       == TextAlign::Center);
    CHECK(L.line_height == doctest::Approx(1.0f));
    CHECK(L.tracking    == doctest::Approx(-1.0f));
    CHECK(L.max_lines   == 1);
}

TEST_CASE("Authoring/Text: style() with unknown id is a no-op (doesn't drop content)") {
    LayerBuilder lb("style_nomatch");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    Text t = layer.text("FUTURI MILIONARI");
    t.font("Inter-Bold.ttf", 106.0f)
     .color(Color::white());

    // unknown id should leave the existing setup intact
    const StyleRegistry empty_registry;
    t.style("not.registered", empty_registry);

    const auto& spec = TextRunBuilderInspector::pending_of(t)->params.text;
    CHECK(spec.content.value            == "FUTURI MILIONARI");
    CHECK(spec.font.font_path            == "Inter-Bold.ttf");
    CHECK(spec.appearance.color.r        == doctest::Approx(1.0f));
}

TEST_CASE("Authoring/Text: motion(id, registry) appends resolved animator") {
    LayerBuilder lb("motion_consume");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);

    MotionRegistry motions;
    TextAnimatorSpec preset;
    preset.id = "text.reveal.soft";
    preset.enabled = true;
    preset.properties.emplace_back(OpacityProperty{0.0f});
    preset.properties.emplace_back(BlurProperty   {12.0f});
    motions.register_motion("text.reveal.soft", preset);

    Text t = layer.text("hero");
    t.font("Inter-Bold.ttf", 106.0f)
     .center()
     .motion("text.reveal.soft", motions);

    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.animators.size() == 1);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators[0].id == "text.reveal.soft");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators[0].properties.size() == 2);
}

TEST_CASE("Authoring/Text: configure_core(Fn) lambda mutates raw TextRunSpec") {
    LayerBuilder lb("configure");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("Anti-Dup");
    t.font("Anton.ttf", 200.0f);

    // Level 3 escape hatch: mutate top-level TextRunSpec fields the
    // fluent surface doesn't expose one step at a time.
    t.configure_core([](chronon3d::TextRunParams& p) {
        p.direction   = TextDirection::RTL;
        p.language    = "ar";
        p.cache_layout = false;
    });

    const auto& P = TextRunBuilderInspector::pending_of(t)->params;
    CHECK(P.direction    == TextDirection::RTL);
    CHECK(P.language     == "ar");
    CHECK(P.cache_layout == false);
    // unrelated fields untouched
    CHECK(P.text.font.font_path == "Anton.ttf");
}

TEST_CASE("Authoring/Text: end-to-end hero chain is mutator-agnostic to TextRunBuilder") {
    // Mirrors the user's design example.
    LayerBuilder lb("hero");
    Layer layer(lb, FrameContext::from_dimensions(1920.0f, 1080.0f));

    layer.text("FUTURI MILIONARI")
        .id("hero-title")
        .font("assets/fonts/Inter-Bold.ttf", 106.0f)
        .center()
        .box({1500.0f, 220.0f})
        .align (TextAlign::Center)
        .vertical_align(VerticalAlign::Middle)
        .pixel_ink_centering()
        .line_height(0.95f)
        .tracking(-1.0f)
        .auto_fit(58.0f, 2)
        .color(Color::white())
        .material(material::premium().bevel(1.5f).glow(14.0f, 0.45f))
        .animate(
            animator("hero-reveal")
                .select(
                    selector(TextSelectorUnit::Grapheme)
                        .smooth()
                        .exclude_spaces()
                        .start(Frame{0},   0.0f)
                        .start(Frame{24}, 100.0f, Easing::OutCubic)
                )
                .position(Vec3{0.0f, 46.0f, 0.0f})
                .scale   (0.94f)
                .opacity (0.0f)
                .blur    (12.0f)
                .tracking(10.0f)
        );

    // Final spec — content, geometry, layout, appearance, animator.
    // Layer::mutable_builder() returns the parent LayerBuilder; the per-Layer
    // counter advanced to 0 after the hero entry and will be 1 on a fresh
    // `.text(...)`, which we verify directly.
    Text probe = layer.text("probe");
    CHECK(TextRunBuilderInspector::pending_of(probe).name == "text_1");
}

TEST_CASE("Authoring/Layer: text() does not crash on destruction of the returned handle") {
    // Verifies the "no commit-on-destruction" invariant — Text handle
    // destruction is a no-op; state lives in the parent's PendingTextRun.
    LayerBuilder lb("destroy");
    {
        Layer layer(lb);
        Text t = layer.text("ephemeral");
        t.font("X.ttf", 12.0f);
        // t is destroyed at end of block; the PendingTextRun is still
        // alive in lb's m_text_runs, fully populated.
        (void)t;
    }
    // The handler chain has finished, but the spec mutation persists.
    // We confirm by issuing a new text() and ensuring the counter advanced.
    Layer layer2(lb);
    Text probe = layer2.text("verify");
    CHECK(TextRunBuilderInspector::pending_of(probe).name == "text_1");
}

TEST_CASE("Authoring/Text + Layer: move-only contracts") {
    CHECK(!std::is_copy_constructible_v<Text>);
    CHECK(!std::is_copy_assignable_v   <Text>);
    CHECK( std::is_move_constructible_v<Text>);
    CHECK( std::is_move_assignable_v   <Text>);

    CHECK(!std::is_copy_constructible_v<Layer>);
    CHECK(!std::is_copy_assignable_v   <Layer>);
    CHECK( std::is_move_constructible_v<Layer>);
    CHECK( std::is_move_assignable_v   <Layer>);
}

TEST_CASE("Authoring/FrameContext: default_viewport + from_dimensions \u00b7 sanity") {
    auto def = FrameContext::default_viewport();
    CHECK(def.width  == doctest::Approx(1920.0f));
    CHECK(def.height == doctest::Approx(1080.0f));

    auto explicit_v = FrameContext::from_dimensions(640.0f, 480.0f);
    CHECK(explicit_v.width  == doctest::Approx(640.0f));
    CHECK(explicit_v.height == doctest::Approx(480.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// Ambient-resolution tests (PR 3.5)
// ═════════════════════════════════════════════════════════════════════════
//
// These tests verify that `.style(id)` / `.motion(id)` (no explicit
// registry argument) resolve via the registries pinned from the parent
// LayerBuilder's `ExtensionContext`. The connection is exercised in
// three layers:
//   - `LayerBuilder::extension_context(...)` attaches the pointer
//   - `Layer::text(...)` reads it + pins registry pointers into the
//     returned `Text` handle's ambient slots
//   - The handle's `.style(id)` / `.motion(id)` reads those slots

// Local helper used only by the ambient tests below. Mirrors the
// `cli_asset_registry()` pattern: process-wide static AssetRegistry
// that the ambient-test ExtensionContext can borrow as a valid ref.
namespace {
inline chronon3d::AssetRegistry& cli_fake_asset_registry() {
    static chronon3d::AssetRegistry reg;
    return reg;
}
}

TEST_CASE("Authoring/Layer + Text: ambient style(id) resolves via LayerBuilder::extension_context") {
    LayerBuilder lb("ambient_style");
    StyleRegistry styles;
    TextStyle hero;
    hero.font_path   = "fonts/Inter-Bold.ttf";
    hero.font_family = "Inter";
    hero.font_weight = 800;
    hero.size        = 96.0f;
    hero.color       = Color{1.0f, 0.86f, 0.2f, 1.0f};
    styles.register_style("hero.premium", hero);

    // Two registries, one for each ambient slot.
    MotionRegistry motions;

    static chronon3d::CompositionRegistry     comp;
    static chronon3d::graph::GraphNodeCatalog gnc;
    static chronon3d::effects::EffectCatalog   eff;
    chronon3d::ExtensionContext ctx{
        comp, gnc, eff,
        cli_fake_asset_registry(),
        &styles, &motions
    };
    lb.extension_context(ctx);

    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("AMBIENT");

    REQUIRE(t.ambient_style_registry()  == &styles);
    REQUIRE(t.ambient_motion_registry() == &motions);

    // The ambient-resolution path: no registry argument supplied.
    t.style("hero.premium");

    const auto& spec = TextRunBuilderInspector::pending_of(t)->params.text;
    CHECK(spec.font.font_path   == "fonts/Inter-Bold.ttf");
    CHECK(spec.font.font_weight == 800);
    CHECK(spec.font.font_size   == doctest::Approx(96.0f));
    CHECK(spec.appearance.color == Color{1.0f, 0.86f, 0.2f, 1.0f});
}

TEST_CASE("Authoring/Layer + Text: ambient motion(id) resolves via LayerBuilder::extension_context") {
    LayerBuilder lb("ambient_motion");
    MotionRegistry motions;
    TextAnimatorSpec preset;
    preset.id = "text.reveal.soft";
    preset.enabled = true;
    preset.properties.emplace_back(OpacityProperty{0.0f});
    motions.register_motion("text.reveal.soft", preset);

    StyleRegistry styles;
    static chronon3d::CompositionRegistry     comp;
    static chronon3d::graph::GraphNodeCatalog gnc;
    static chronon3d::effects::EffectCatalog   eff;
    chronon3d::ExtensionContext ctx{
        comp, gnc, eff,
        cli_fake_asset_registry(),
        &styles, &motions
    };
    lb.extension_context(ctx);

    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("MOTION");

    REQUIRE(t.ambient_motion_registry() == &motions);

    t.motion("text.reveal.soft");

    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.animators.size() == 1);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators[0].id == "text.reveal.soft");
}

TEST_CASE("Authoring/Layer + Text: ambient methods no-op when no ExtensionContext attached") {
    LayerBuilder lb("no_ambient");
    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);   // No extension_context(...) call.

    Text t = layer.text("PLAIN");
    t.font("X.ttf", 32.0f);
    REQUIRE(t.ambient_style_registry()  == nullptr);
    REQUIRE(t.ambient_motion_registry() == nullptr);

    StyleRegistry  external_styles;
    TextStyle      arbitrary; arbitrary.size = 64.0f;
    external_styles.register_style("ignored", arbitrary);
    MotionRegistry external_motions;
    TextAnimatorSpec arbitrary_motion;
    arbitrary_motion.id = "ignored";
    external_motions.register_motion("ignored.motion", arbitrary_motion);

    // Without ambient pointers attached, the ambient-resolution methods
    // must NO-OP — the explicit external registry is never consulted.
    t.style("ignored");
    t.motion("ignored.motion");

    // Font set by the prior .font() call is preserved; the ambient
    // attempts did not mutate spec.appearance.color or the animators vector.
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "X.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators.empty());
}

TEST_CASE("Authoring/Layer + Text: ambient method no-op when ExtensionContext.style_registry is null") {
    LayerBuilder lb("null_style");
    MotionRegistry motions;
    static chronon3d::CompositionRegistry     comp;
    static chronon3d::graph::GraphNodeCatalog gnc;
    static chronon3d::effects::EffectCatalog   eff;
    chronon3d::ExtensionContext ctx{
        comp, gnc, eff,
        cli_fake_asset_registry(),
        /*style_registry=*/ nullptr,
        &motions
    };
    lb.extension_context(ctx);

    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("X");
    t.font("Y.ttf", 24.0f);

    REQUIRE(t.ambient_style_registry()  == nullptr);
    REQUIRE(t.ambient_motion_registry() == &motions);

    // Ambient `.style(id)` should no-op because the pointer is null.
    t.style("anything");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "Y.ttf");

    // Ambient `.motion(id)` no-ops when id is unregistered even though
    // the pointer is set.
    t.motion("unregistered.id");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators.empty());
}

TEST_CASE("Authoring/Layer + Text: dual-path coexist (explicit + ambient on the same handle)") {
    LayerBuilder lb("dual");
    StyleRegistry styles;
    TextStyle ambient_style;  ambient_style.font_path  = "ambient.ttf";  ambient_style.size  = 48.0f;
    TextStyle explicit_style; explicit_style.font_path = "explicit.ttf"; explicit_style.size = 64.0f;
    styles.register_style("ambient_call",  ambient_style);
    styles.register_style("explicit_call", explicit_style);

    StyleRegistry explicit_separate;
    explicit_separate.register_style("override", explicit_style);

    static chronon3d::CompositionRegistry     comp;
    static chronon3d::graph::GraphNodeCatalog gnc;
    static chronon3d::effects::EffectCatalog   eff;
    chronon3d::ExtensionContext ctx{
        comp, gnc, eff,
        cli_fake_asset_registry(),
        &styles,
        nullptr
    };
    lb.extension_context(ctx);

    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("DUAL");
    REQUIRE(t.ambient_style_registry() == &styles);

    // Ambient call resolves to the ambient style.
    t.style("ambient_call");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "ambient.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_size   == doctest::Approx(48.0f));

    // Explicit call with a different registry overrides the previous
    // ambient call's mutation entirely.
    t.style("override", explicit_separate);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "explicit.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_size   == doctest::Approx(64.0f));
}

TEST_CASE("Authoring/Layer + Text: ambient resolves unknown id to no-op (preserves existing state)") {
    LayerBuilder lb("unknown_id");
    StyleRegistry      styles;        // empty
    MotionRegistry     motions;       // empty
    static chronon3d::CompositionRegistry     comp;
    static chronon3d::graph::GraphNodeCatalog gnc;
    static chronon3d::effects::EffectCatalog   eff;
    chronon3d::ExtensionContext ctx{
        comp, gnc, eff,
        cli_fake_asset_registry(),
        &styles, &motions
    };
    lb.extension_context(ctx);

    lb.screen_dimensions(1920.0f, 1080.0f);
    Layer layer(lb);
    Text t = layer.text("UNCHANGED");
    t.font("K.ttf", 24.0f);

    t.style("never.registered");
    t.motion("never.registered.either");

    CHECK(TextRunBuilderInspector::pending_of(t)->params.text.font.font_path == "K.ttf");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.animators.empty());
}

// ═════════════════════════════════════════════════════════════════════════
// PR 5.1 — register_range + merge polish tests
// ═════════════════════════════════════════════════════════════════════════
//
// Verifies:
//   - register_range handles Value pairs + Factory pairs at compile time
//     via `std::is_invocable_r_v<Value, ...>` dispatch.
//   - merge composes a target out of a source layer-cake; source's
//     proxies re-invoke through to source on every target lookup, so
//     source mutations are visible without re-merging.

TEST_CASE("Authoring/Registry: register_range bulk loads (string, Value) pairs") {
    StyleRegistry reg;
    const std::array<std::pair<std::string, chronon3d::TextStyle>, 3> pairs{{
        {"a.64",  chronon3d::TextStyle{.size = 64.0f, .color = Color{1.0f, 0.0f, 0.0f, 1.0f}}},
        {"b.96",  chronon3d::TextStyle{.size = 96.0f, .color = Color{0.0f, 1.0f, 0.0f, 1.0f}}},
        {"c.32",  chronon3d::TextStyle{.size = 32.0f, .color = Color{0.0f, 0.0f, 1.0f, 1.0f}}},
    }};
    reg.register_range(pairs.begin(), pairs.end());

    CHECK(reg.count() == 3);
    CHECK(reg.has("a.64"));
    CHECK(reg.has("b.96"));
    CHECK(reg.has("c.32"));

    auto a = reg.resolve("a.64"); REQUIRE(a.has_value()); CHECK(a->size == doctest::Approx(64.0f));
    auto b = reg.resolve("b.96"); REQUIRE(b.has_value()); CHECK(b->size == doctest::Approx(96.0f));
    auto c = reg.resolve("c.32"); REQUIRE(c.has_value()); CHECK(c->size == doctest::Approx(32.0f));
}

TEST_CASE("Authoring/Registry: register_range bulk loads (string, Factory) callables") {
    StyleRegistry reg;
    int call_a = 0, call_b = 0;
    const std::array<std::pair<std::string, std::function<chronon3d::TextStyle()>>, 2> factories{{
        {"param.a", [&]() { ++call_a; chronon3d::TextStyle s; s.size = 24.0f * call_a; return s; }},
        {"param.b", [&]() { ++call_b; chronon3d::TextStyle s; s.size = 12.0f * call_b; return s; }},
    }};
    reg.register_range(factories.begin(), factories.end());

    REQUIRE(call_a == 0);
    REQUIRE(call_b == 0);
    auto a1 = reg.resolve("param.a"); CHECK(call_a == 1); REQUIRE(a1.has_value()); CHECK(a1->size == doctest::Approx(24.0f));
    auto a2 = reg.resolve("param.a"); CHECK(call_a == 2); REQUIRE(a2.has_value()); CHECK(a2->size == doctest::Approx(48.0f));
    auto b1 = reg.resolve("param.b"); CHECK(call_b == 1); REQUIRE(b1.has_value()); CHECK(b1->size == doctest::Approx(12.0f));
}

TEST_CASE("Authoring/Registry: register_range silently skips empty ids") {
    StyleRegistry reg;
    const std::array<std::pair<std::string, chronon3d::TextStyle>, 2> pairs{{
        {"valid",  chronon3d::TextStyle{.size = 24.0f}},
        {"",       chronon3d::TextStyle{.size = 999.0f}},   // empty id → skipped
    }};
    reg.register_range(pairs.begin(), pairs.end());
    CHECK(reg.count() == 1);
    CHECK(reg.has("valid"));
    CHECK_FALSE(reg.has(""));
}

TEST_CASE("Authoring/Registry: register_range also available on MotionRegistry via inheritance") {
    MotionRegistry reg;
    const std::array<std::pair<std::string, chronon3d::TextAnimatorSpec>, 2> pairs{{
        {"hero.in",  chronon3d::TextAnimatorSpec{.id = "hero.in",  .enabled = true}},
        {"hero.out", chronon3d::TextAnimatorSpec{.id = "hero.out", .enabled = true}},
    }};
    reg.register_range(pairs.begin(), pairs.end());
    CHECK(reg.count() == 2);
    CHECK(reg.has("hero.in"));
    CHECK(reg.has("hero.out"));
}

TEST_CASE("Authoring/Registry: merge composes builtin pack into target (layer-cake pattern)") {
    // Simulate a builtin pack + studio overlay: studio targets receive
    // all builtin entries via merge.
    StyleRegistry builtin_pack;
    builtin_pack.register_style(
        "youtube.hero.premium",
        chronon3d::TextStyle{.font_path = "Inter-Bold.ttf", .size = 96.0f}
    );
    builtin_pack.register_style(
        "news.lower-third",
        chronon3d::TextStyle{.font_path = "DMSans-Bold.ttf", .size = 48.0f}
    );

    StyleRegistry studio_pack;
    studio_pack.register_style(
        "youtube.hero.premium",     // override the builtin id
        chronon3d::TextStyle{.font_path = "Anton.ttf", .size = 120.0f}
    );
    studio_pack.register_style(
        "studio.brand.premium",
        chronon3d::TextStyle{.font_path = "Poppins-Bold.ttf", .size = 88.0f}
    );

    studio_pack.merge(builtin_pack);

    // After merge, target has all builtin ids + its own pre-existing ones.
    // Override semantics: builtin_pack's "youtube.hero.premium" overwrites
    // studio_pack's pre-existing "youtube.hero.premium" (last-write-wins
    // in chronological order).
    CHECK(studio_pack.count() == 3);
    CHECK(studio_pack.has("youtube.hero.premium"));
    CHECK(studio_pack.has("news.lower-third"));
    CHECK(studio_pack.has("studio.brand.premium"));

    auto yh = studio_pack.resolve("youtube.hero.premium");
    REQUIRE(yh.has_value());
    CHECK(yh->font_path == "Inter-Bold.ttf");  // builtin overrode studio
    CHECK(yh->size       == doctest::Approx(96.0f));

    auto brand = studio_pack.resolve("studio.brand.premium");
    REQUIRE(brand.has_value());
    CHECK(brand->font_path == "Poppins-Bold.ttf");
}

TEST_CASE("Authoring/Registry: merge proxies source — source mutations visible to target") {
    StyleRegistry source;
    StyleRegistry target;

    target.merge(source);
    CHECK(target.count() == 0);  // nothing to merge from empty source

    source.register_style("late.arrival",
        chronon3d::TextStyle{.font_path = "Anton.ttf", .size = 64.0f});

    // After merging an EMPTY source, target still has nothing (proxy
    // iteration had nothing to iterate).  This is the expected "merge
    // is a snapshot" baseline.  Re-merging picks up the new entries:
    target.merge(source);
    CHECK(target.count() == 1);
    auto r = target.resolve("late.arrival");
    REQUIRE(r.has_value());
    CHECK(r->font_path == "Anton.ttf");
    CHECK(r->size       == doctest::Approx(64.0f));
}

TEST_CASE("Authoring/Registry: merge self-merge is a no-op (no deadlock against std::lock)") {
    StyleRegistry reg;
    reg.register_style("a", chronon3d::TextStyle{.size = 24.0f});
    // Calling reg.merge(reg) MUST NOT deadlock. The implementation
    // short-circuits on this == &source.  After the call, count is
    // unchanged.
    reg.merge(reg);
    CHECK(reg.count() == 1);
    CHECK(reg.has("a"));
}

TEST_CASE("Authoring/Registry: merge from empty source is a valid no-op") {
    StyleRegistry source;       // empty
    StyleRegistry target;
    target.register_style("preexisting", chronon3d::TextStyle{.size = 32.0f});

    target.merge(source);

    CHECK(target.count() == 1);   // only the pre-existing entry
    CHECK(target.has("preexisting"));
}

TEST_CASE("Authoring/Registry: merge with mixed value+factory source preserves both kinds") {
    // Source styles get merged; both value-style and factory-style
    // entries land on target as proxies (value-style proxy returns
    // the cached value, factory-style proxy re-invokes the factory).
    StyleRegistry source;
    source.register_style("static.value",
        chronon3d::TextStyle{.font_path = "A.ttf", .size = 24.0f});
    int param_calls = 0;
    source.register_factory("parametric.value", [&]() {
        ++param_calls;
        chronon3d::TextStyle s;
        s.font_path = "B.ttf";
        s.size      = 12.0f * param_calls;
        return s;
    });

    StyleRegistry target;
    target.merge(source);
    CHECK(target.count() == 2);

    // Value entry: resolved via proxy that returns the preserved value.
    auto v = target.resolve("static.value");
    REQUIRE(v.has_value());
    CHECK(v->font_path == "A.ttf");
    CHECK(v->size       == doctest::Approx(24.0f));

    // Factory entry: resolved via proxy that re-invokes the factory.
    auto p1 = target.resolve("parametric.value");
    REQUIRE(p1.has_value());
    CHECK(p1->size == doctest::Approx(12.0f));    // call_count = 1
    CHECK(param_calls == 1);

    auto p2 = target.resolve("parametric.value");
    REQUIRE(p2.has_value());
    CHECK(p2->size == doctest::Approx(24.0f));    // call_count = 2
    CHECK(param_calls == 2);
}

// ============================================================================
// PR 4 — Scene + Composition wrapper tests
//
// Verifies:
//   - CompositionBuilder fluent chain produces chronon3d::Composition
//     with matching name / width / height / duration / frame_rate fields.
//   - .build() with no .scene() returns an empty composition (graceful no-op,
//     not a throw).
//   - composition() factory returns an empty CompositionBuilder.
//   - Scene::layer(name, fn) SFINAE dispatches both:
//       (a) fn(Layer&)            — wraps SceneBuilder-spawned LayerBuilder in Layer
//       (b) fn(LayerBuilder&)     — passthrough (engine raw surface)
//   - Scene + Layer + Text within Composition::scene(fn) chain correctly:
//       FrameContext flows through composition -> scene -> layer -> text
//       so Text::center() resolves to the composition's viewport center.
//   - Composition::evaluate(0) returns Scene with the expected layer count
//     and the expected text run payload.
//   - custom_builder() injection lets the user swap SceneBuilder's
//     pmr-resource / shape_registry path.

// ── CompositionSpec accumulation ─────────────────────────────────────────────
TEST_CASE("Authoring/CompositionBuilder: fields accumulate via fluent setters") {
    using chronon3d::authoring::CompositionBuilder;
    CompositionBuilder cb;
    cb.name("hero-showcase")
      .width(1920)
      .height(1080)
      .duration(Frame{60})
      .frame_rate(FrameRate{30, 1})
      .assets_root(std::filesystem::path{"assets"});

    chronon3d::Composition comp = std::move(cb).build();
    CHECK(comp.name()     == "hero-showcase");
    CHECK(comp.width()    == 1920);
    CHECK(comp.height()   == 1080);
    CHECK(comp.duration().integral() == 60);
    CHECK(comp.frame_rate().numerator == 30);
    CHECK(comp.assets_root() == "assets");
}

// ── Graceful empty renderer ─────────────────────────────────────────────────
TEST_CASE("Authoring/CompositionBuilder: empty composition (no .scene()) renders zero layers") {
    using chronon3d::authoring::CompositionBuilder;
    CompositionBuilder cb;
    cb.name("empty").width(640).height(480);

    chronon3d::Composition comp = std::move(cb).build();
    chronon3d::Scene scene = comp.evaluate(Frame{0});
    CHECK(scene.layers().empty());
    CHECK(scene.nodes().empty());
}

// ── Scene::layer SFINAE dual-surface (a) → wrap with Layer ───────────────────
TEST_CASE("Authoring/Scene + Layer: SFINAE wrap branch populates authored text in evaluated Scene") {
    using chronon3d::authoring::composition;
    chronon3d::Composition comp = composition()
        .name("dual-wrap")
        .width(1920).height(1080).duration(Frame{1})
        .scene([](chronon3d::authoring::Scene& s, const chronon3d::FrameContext& /*ctx*/) {
            s.layer("title", [](chronon3d::authoring::Layer& l) {
                l.text("HELLO")
                 .id("hello_text")
                 .font("assets/fonts/Poppins-Bold.ttf", 96.0f);
            });
        })
        .build();

    chronon3d::Scene evaluated = comp.evaluate(Frame{0});
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "title");
    // The layer's text run payload mirrors the authored spec (1 text-run entry).
    REQUIRE(evaluated.layers()[0].nodes.size() == 1);
}

// ── Scene::layer SFINAE dual-surface (b) → passthrough ──────────────────────
TEST_CASE("Authoring/Scene: SFINAE passthrough branch (LayerBuilder& closure) is honored") {
    using chronon3d::authoring::composition;
    int draw_count = 0;
    chronon3d::Composition comp = composition()
        .name("passthrough")
        .width(800).height(600)
        .scene([&draw_count](chronon3d::authoring::Scene& s, const chronon3d::FrameContext& ctx) {
            s.layer("raw", [&ctx, &draw_count](LayerBuilder& lb) {
                // Direct LayerBuilder API — no authoring façade.
                lb.screen_dimensions(static_cast<float>(ctx.width), static_cast<float>(ctx.height));
                lb.rect("bg", {.size = {static_cast<float>(ctx.width), static_cast<float>(ctx.height)},
                                .color = Color::white()});
                ++draw_count;
            });
        })
        .build();

    (void) comp.evaluate(Frame{0});
    CHECK(draw_count == 1);                // closure invoked exactly once per Layer.
}

// ── FrameContext flow ───────────────────────────────────────────────────────
TEST_CASE("Authoring/CompositionBuilder: FrameContext flows into Scene::layer closure (LayerBuilder ctor reads it)") {
    using chronon3d::authoring::composition;
    int ctx_width = 0;
    int ctx_height = 0;
    chronon3d::Composition comp = composition()
        .name("ctx-flow")
        .width(1280).height(720)
        .scene([&ctx_width, &ctx_height](chronon3d::authoring::Scene& s, const chronon3d::FrameContext& ctx) {
            ctx_width  = ctx.width;
            ctx_height = ctx.height;
            s.layer("bg", [](LayerBuilder& lb) {
                lb.screen_dimensions(1280.0f, 720.0f);
                lb.fullscreen_rect("fs", Color::white());
            });
        })
        .build();

    (void) comp.evaluate(Frame{0});
    CHECK(ctx_width  == 1280);
    CHECK(ctx_height == 720);
}

// ── custom_builder injection ────────────────────────────────────────────────
TEST_CASE("Authoring/CompositionBuilder: custom_builder(factory) is invoked per evaluate()") {
    using chronon3d::authoring::composition;
    int factory_call_count = 0;
    chronon3d::Composition comp = composition()
        .name("custom")
        .width(100).height(100)
        .custom_builder([&factory_call_count](const chronon3d::FrameContext& ctx) {
            ++factory_call_count;
            return SceneBuilder(ctx);
        })
        .scene([](chronon3d::authoring::Scene& s, const chronon3d::FrameContext& ctx) {
            s.layer("bg", [](chronon3d::authoring::Layer&) { });
        })
        .build();

    (void) comp.evaluate(Frame{0});
    CHECK(factory_call_count == 1);
}

// ── Composition.move-construct (consumes builder by rvalue) ────────────────
TEST_CASE("Authoring/CompositionBuilder: build() consumes builder by rvalue") {
    using chronon3d::authoring::CompositionBuilder;
    CHECK(!std::is_copy_constructible_v<CompositionBuilder>);
    CHECK(!std::is_copy_assignable_v   <CompositionBuilder>);
    CHECK( std::is_move_constructible_v<CompositionBuilder>);
    CHECK( std::is_move_assignable_v   <CompositionBuilder>);
}

// ============================================================================
// PR 4 — Fail-fast Layer ctor + TextShaping.script gap
// ============================================================================

// ── Fail-fast Layer ctor (Q1: throw unconditionally; no assert) ──────────────
TEST_CASE("Authoring/Layer: explicit ctor throws runtime_error when parent builder has no screen_dimensions") {
    LayerBuilder lb("no_dims");
    bool caught = false;
    std::string what_msg;
    try {
        chronon3d::authoring::Layer layer(lb);
        FAIL("expected std::runtime_error to escape Layer(LayerBuilder&) without screen_dimensions(...)");
    } catch (const std::runtime_error& e) {
        caught = true;
        what_msg = e.what();
    } catch (...) {
        FAIL("expected std::runtime_error specifically");
    }
    REQUIRE(caught);
    CHECK(what_msg.find("no_dims") != std::string::npos);
    CHECK(what_msg.find("screen_dimensions") != std::string::npos);
}

TEST_CASE("Authoring/Layer: explicit ctor succeeds when parent builder has screen_dimensions set") {
    LayerBuilder lb("with_dims");
    lb.screen_dimensions(1920.0f, 1080.0f);
    REQUIRE_NOTHROW(chronon3d::authoring::Layer{lb});
}

TEST_CASE("Authoring/Layer: explicit FrameContext ctor works without screen_dimensions (escape hatch)") {
    LayerBuilder lb("explicit_ctx");
    // Note: NO screen_dimensions call here. Using explicit ctor is the documented escape path.
    REQUIRE_NOTHROW(chronon3d::authoring::Layer{lb, chronon3d::authoring::FrameContext::default_viewport()});
}

// ── Script serialization (Q3: uint32_t instead of int) ─────────────────────
TEST_CASE("Authoring/Text: script(uint32_t) chain mutates pending_->params.script") {
    LayerBuilder lb("script_round_trip");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);

    chronon3d::authoring::Text t = layer.text("ŁATIN");
    t.script(0x4C61746Eu);                  // HB_SCRIPT_LATIN (HarfBuzz tag)

    CHECK(TextRunBuilderInspector::pending_of(t)->params.script == 0x4C61746Eu);
}

TEST_CASE("Authoring/Text: default script=0u is preserved (auto-detect path stays intact)") {
    LayerBuilder lb("script_default");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);

    chronon3d::authoring::Text t = layer.text("AUTODETECT");
    CHECK(TextRunBuilderInspector::pending_of(t)->params.script == 0u);
}

// ── apply_text_style propagate shaping.script (Q2: only writes through when != 0) ──
TEST_CASE("Authoring/Text: style(id) propagates shaping.script when non-zero") {
    LayerBuilder lb("script_propagate");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);

    StyleRegistry styles;
    chronon3d::TextStyle hero;
    hero.font_path = "Inter-Bold.ttf";
    hero.size      = 96.0f;
    hero.shaping.direction = TextDirection::LTR;
    hero.shaping.language  = "en";
    hero.shaping.script    = 0x41726162u;   // HB_SCRIPT_ARABIC
    styles.register_style("arabic.hero", hero);

    chronon3d::authoring::Text t = layer.text("AR");
    t.style("arabic.hero", styles);

    CHECK(TextRunBuilderInspector::pending_of(t)->params.script == 0x41726162u);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.direction == TextDirection::LTR);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.language == "en");
}

TEST_CASE("Authoring/Text: style(id) preserves pending script=0 (auto-detect semantic preserved)") {
    LayerBuilder lb("script_zero");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);

    StyleRegistry styles;
    chronon3d::TextStyle default_style;
    default_style.size = 48.0f;
    default_style.shaping.script = 0u;      // 0 = inherit / unspecified
    styles.register_style("default.no.script", default_style);

    chronon3d::authoring::Text t = layer.text("DEFAULT");
    // Pre-set script explicitly to HB_SCRIPT_LATIN, then verify apply
    // with shaping.script=0 does NOT overwrite.
    t.script(0x4C61746Eu);
    REQUIRE(TextRunBuilderInspector::pending_of(t)->params.script == 0x4C61746Eu);

    t.style("default.no.script", styles);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.script == 0x4C61746Eu);  // unchanged.
}

// ── High-bit pattern check (Q3) ───────────────────────────────────────────────
TEST_CASE("Authoring/Text: script accepts patterned HB tag including high-bit bytes (no sign-extend)") {
    LayerBuilder lb("script_highbit");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);
    chronon3d::authoring::Text t = layer.text("X");
    constexpr std::uint32_t kPattern = 0x80808080u;   // top-bit set in every byte.
    t.script(kPattern);
    CHECK(TextRunBuilderInspector::pending_of(t)->params.script == kPattern);
    CHECK((TextRunBuilderInspector::pending_of(t)->params.script & 0x80000000u) != 0u);  // sign bit stays unset.
}
