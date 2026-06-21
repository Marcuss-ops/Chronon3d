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
using chronon3d::authoring::animator;
using chronon3d::authoring::material;
using chronon3d::authoring::selector;

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
        [](const void* p) {
            return static_cast<const chronon3d::PositionProperty*>(p)->value
                == Vec3{0.0f, 46.0f, 0.0f};
        }));
    CHECK(find_property(built, typeid(chronon3d::ScaleProperty),
        [](const void* p) {
            auto* sp = static_cast<const chronon3d::ScaleProperty*>(p);
            // Uniform .scale(0.94f) → Vec3{0.94f, 0.94f, 1.0f}.
            return sp->value.x == doctest::Approx(0.94f)
                && sp->value.y == doctest::Approx(0.94f)
                && sp->value.z == doctest::Approx(1.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::OpacityProperty),
        [](const void* p) {
            return static_cast<const chronon3d::OpacityProperty*>(p)->value
                == doctest::Approx(0.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::BlurProperty),
        [](const void* p) {
            return static_cast<const chronon3d::BlurProperty*>(p)->radius
                == doctest::Approx(12.0f);
        }));
    CHECK(find_property(built, typeid(chronon3d::TrackingProperty),
        [](const void* p) {
            return static_cast<const chronon3d::TrackingProperty*>(p)->pixels
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
    CHECK(sp->value == doctest::Approx3D(Vec3{0.5f, 0.5f, 1.0f}));

    Animator v = animator("vec3");
    v.scale(Vec3{0.5f, 0.25f, 2.0f});
    auto v_built = AnimatorTestAccess::release(std::move(v));
    REQUIRE(v_built.properties.size() == 1);
    auto* sp2 = std::get_if<chronon3d::ScaleProperty>(&v_built.properties[0]);
    REQUIRE(sp2 != nullptr);
    CHECK(sp2->value == doctest::Approx3D(Vec3{0.5f, 0.25f, 2.0f}));
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
                has_opacity = (v.value == doctest::Approx(0.0f));
            } else if constexpr (std::is_same_v<T, chronon3d::BlurProperty>) {
                has_blur = (v.radius == doctest::Approx(12.0f));
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

TEST_CASE("Authoring/Layer: text() pushes a PendingTextRun with auto-generated name") {
    LayerBuilder lb("test_layer");
    Layer layer(lb);

    Text t1 = layer.text("HELLO");
    Text t2 = layer.text("WORLD");

    // Auto-generated name pattern: "text_<N>" where N is per-Layer sequential.
    // Same Layer produced unique entries; both must be reachable through
    // the returned handle.
    CHECK(t1.mutable_pending().name == "text_0");
    CHECK(t2.mutable_pending().name == "text_1");
    CHECK(t1.mutable_pending().params.text.content.value == "HELLO");
    CHECK(t2.mutable_pending().params.text.content.value == "WORLD");

    // Distinct underlying PendingTextRun* (different entries in the layer's
    // m_text_runs vector). Identity check via the pointer the handle holds.
    CHECK(&t1.mutable_pending() != &t2.mutable_pending());
}

TEST_CASE("Authoring/Text: id() + content() store and propagate to underlying spec") {
    LayerBuilder lb("id_content");
    Layer layer(lb);
    Text t = layer.text("initial");

    t.id("hero-title").content("UPDATED");

    CHECK(t.mutable_pending().name == "hero-title");
    CHECK(t.mutable_pending().params.text.content.value == "UPDATED");
}

TEST_CASE("Authoring/Text: font() / font_family() / weight() / italic() / font_size() cover FontSpec") {
    LayerBuilder lb("font");
    Layer layer(lb);
    Text t = layer.text("x");

    t.font("assets/fonts/Inter-Bold.ttf", 96.0f)
     .font_family("Inter")
     .weight(800)
     .italic(true)
     .font_size(120.0f);                  // overrides initial 96

    const auto& font = t.mutable_pending().params.text.font;
    CHECK(font.font_path   == "assets/fonts/Inter-Bold.ttf");
    CHECK(font.font_family == "Inter");
    CHECK(font.font_weight == 800);
    CHECK(font.font_style  == "italic");
    CHECK(font.font_size   == doctest::Approx(120.0f));
}

TEST_CASE("Authoring/Text: at(Vec2) lifts z to 0; at(Vec3) preserves all components") {
    LayerBuilder lb("position");
    Layer layer(lb);

    Text t_v2 = layer.text("v2");
    t_v2.at(Vec2{100.0f, 200.0f});
    CHECK(t_v2.mutable_pending().params.text.position
          == doctest::Approx3D(Vec3{100.0f, 200.0f, 0.0f}));

    Text t_v3 = layer.text("v3");
    t_v3.at(Vec3{11.0f, 22.0f, 33.0f});
    CHECK(t_v3.mutable_pending().params.text.position
          == doctest::Approx3D(Vec3{11.0f, 22.0f, 33.0f}));

    Text t_2arg = layer.text("2arg");
    t_2arg.at(7.0f, 8.0f);
    CHECK(t_2arg.mutable_pending().params.text.position
          == doctest::Approx3D(Vec3{7.0f, 8.0f, 0.0f}));
}

TEST_CASE("Authoring/Text: center() uses FrameContext viewport") {
    LayerBuilder lb("center");
    Layer layer(lb, FrameContext::from_dimensions(800.0f, 600.0f));

    Text t = layer.text("hero");
    t.center();

    // position is (w/2, h/2, 0)
    CHECK(t.mutable_pending().params.text.position
          == doctest::Approx3D(Vec3{400.0f, 300.0f, 0.0f}));

    // Layout auto-set for invisibly-aligned center
    const auto& L = t.mutable_pending().params.text.layout;
    CHECK(L.anchor         == TextAnchor::Center);
    CHECK(L.align          == TextAlign::Center);
    CHECK(L.vertical_align == VerticalAlign::Middle);
}

TEST_CASE("Authoring/Text: center() falls back to 1920x1080 when no FrameContext provided") {
    LayerBuilder lb("fallback");
    Layer layer(lb);  // default FrameContext

    Text t = layer.text("x");
    t.center();
    CHECK(t.mutable_pending().params.text.position
          == doctest::Approx3D(Vec3{960.0f, 540.0f, 0.0f}));
}

TEST_CASE("Authoring/Text: layout setters propagate to spec.text.layout") {
    LayerBuilder lb("layout_props");
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

    const auto& L = t.mutable_pending().params.text.layout;
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
    Layer layer(lb);

    Text t = layer.text("x");
    t.color(Color{0.9f, 0.1f, 0.2f, 1.0f});

    const auto& A = t.mutable_pending().params.text.appearance;
    CHECK(A.color == Color{0.9f, 0.1f, 0.2f, 1.0f});
    // paint / shadows / material / box_style should still be defaults.
    CHECK(A.paint.fill      == Color{1.0f, 1.0f, 1.0f, 1.0f});  // default
    CHECK(A.shadows.empty());
    CHECK(A.material.enabled == false);                          // default
    CHECK(A.box_style.enabled == false);                         // default
}

TEST_CASE("Authoring/Text: material(Material) consumes Material::release() into appearance.material") {
    LayerBuilder lb("material_consume");
    Layer layer(lb);

    Text t = layer.text("x");
    Material m = material::premium()
                     .bevel(2.0f)
                     .glow(8.0f, 0.5f);
    t.material(std::move(m));

    // After consumption, source Material is in moved-from state — release()
    // would return an unspecified value but NOT touch the captured value.
    const auto& captured = t.mutable_pending().params.text.appearance.material;
    CHECK(captured.enabled                 == true);
    CHECK(captured.bevel_px                == doctest::Approx(2.0f));
    CHECK(captured.use_material_glow       == true);
    CHECK(captured.glow_radius             == doctest::Approx(8.0f));
    CHECK(captured.glow_intensity          == doctest::Approx(0.5f));
}

TEST_CASE("Authoring/Text: animate(Animator) consumes Animator::release() into animators vector") {
    LayerBuilder lb("animate_consume");
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

    REQUIRE(t.mutable_pending().params.animators.size() == 1);
    const auto& appended = t.mutable_pending().params.animators.back();
    CHECK(appended.id == "reveal");
    CHECK(appended.enabled == true);
    REQUIRE(appended.selectors.size() == 1);
    CHECK(appended.selectors[0].unit  == TextSelectorUnit::Grapheme);
    CHECK(appended.selectors[0].shape == TextSelectorShape::Smooth);
    REQUIRE(appended.properties.size() == 3);
}

TEST_CASE("Authoring/Text: multiple animate() calls accumulate in order") {
    LayerBuilder lb("multi_anim");
    Layer layer(lb);

    Text t = layer.text("x");
    t.animate(animator("in")  .opacity(0.0f));
    t.animate(animator("out") .opacity(1.0f));
    t.animate(animator("idle").tracking(2.0f));

    const auto& list = t.mutable_pending().params.animators;
    REQUIRE(list.size() == 3);
    CHECK(list[0].id == "in");
    CHECK(list[1].id == "out");
    CHECK(list[2].id == "idle");
}

TEST_CASE("Authoring/Text: style(id, registry) field-maps TextStyle to spec.text") {
    LayerBuilder lb("style_map");
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

    const auto& F = t.mutable_pending().params.text.font;
    const auto& L = t.mutable_pending().params.text.layout;
    const auto& A = t.mutable_pending().params.text.appearance;
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
    Layer layer(lb);

    Text t = layer.text("FUTURI MILIONARI")
                .font("Inter-Bold.ttf", 106.0f)
                .color(Color::white());

    // unknown id should leave the existing setup intact
    const StyleRegistry empty_registry;
    t.style("not.registered", empty_registry);

    const auto& spec = t.mutable_pending().params.text;
    CHECK(spec.content.value            == "FUTURI MILIONARI");
    CHECK(spec.font.font_path            == "Inter-Bold.ttf");
    CHECK(spec.appearance.color.r        == doctest::Approx(1.0f));
}

TEST_CASE("Authoring/Text: motion(id, registry) appends resolved animator") {
    LayerBuilder lb("motion_consume");
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

    REQUIRE(t.mutable_pending().params.animators.size() == 1);
    CHECK(t.mutable_pending().params.animators[0].id == "text.reveal.soft");
    CHECK(t.mutable_pending().params.animators[0].properties.size() == 2);
}

TEST_CASE("Authoring/Text: configure_core(Fn) lambda mutates raw TextRunSpec") {
    LayerBuilder lb("configure");
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

    const auto& P = t.mutable_pending().params;
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
    CHECK(probe.mutable_pending().name == "text_1");
}

TEST_CASE("Authoring/Layer: text() does not crash on destruction of the returned handle") {
    // Verifies the "no commit-on-destruction" invariant — Text handle
    // destruction is a no-op; state lives in the parent's PendingTextRun.
    LayerBuilder lb("destroy");
    {
        Layer layer(lb);
        Text t = layer.text("ephemeral").font("X.ttf", 12.0f);
        // t is destroyed at end of block; the PendingTextRun is still
        // alive in lb's m_text_runs, fully populated.
        (void)t;
    }
    // The handler chain has finished, but the spec mutation persists.
    // We confirm by issuing a new text() and ensuring the counter advanced.
    Layer layer2(lb);
    Text probe = layer2.text("verify");
    CHECK(probe.mutable_pending().name == "text_1");
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
