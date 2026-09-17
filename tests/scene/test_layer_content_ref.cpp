// test_layer_content_ref.cpp — ADR-031 P1: the (content, role) view of LayerKind.
//
// P1 adds no behaviour: `Layer::kind` is still the storage and
// `Layer::content()` / `Layer::role()` are pure functions of it. What this test
// pins is the taxonomy (TICKET-LAYER-CONTENT-REF §3), because P2 will rewrite
// readers onto `carries_drawable_content()` and any drift here becomes a silent
// layer loss (TICKET-104: the coordinates builder keyed on `LayerKind::Normal`
// and silently dropped Shape layers).
//
// All 10 enum values are enumerated. `content_of` / `role_of` deliberately carry
// no `default:` arm, so adding an 11th `LayerKind` is a `-Wswitch` warning at
// the mapping site — this file then fails to be the thing that stays correct.

#include <doctest/doctest.h>

#include <chronon3d/scene/model/layer/layer.hpp>

#include <array>
#include <cstddef>

using chronon3d::ContentKind;
using chronon3d::ContentRef;
using chronon3d::Layer;
using chronon3d::LayerKind;
using chronon3d::LayerRole;

namespace {

struct Expectation {
    LayerKind kind;
    const char* name;
    ContentKind content;
    LayerRole role;
    bool draws;
};

// Taxonomy signed off in TICKET-LAYER-CONTENT-REF §3 — one row per enum value.
constexpr std::array<Expectation, 10> kTaxonomy{{
    {LayerKind::Normal,     "Normal",     ContentKind::Image,       LayerRole::Rendered,   true},
    {LayerKind::Adjustment, "Adjustment", ContentKind::None,        LayerRole::Adjustment, false},
    {LayerKind::Null,       "Null",       ContentKind::None,        LayerRole::Null,       false},
    {LayerKind::Precomp,    "Precomp",    ContentKind::Composition, LayerRole::Rendered,   true},
    {LayerKind::Video,      "Video",      ContentKind::Video,       LayerRole::Rendered,   true},
    {LayerKind::Glass,      "Glass",      ContentKind::Image,       LayerRole::Rendered,   true},
    {LayerKind::Shape,      "Shape",      ContentKind::Shape,       LayerRole::Rendered,   true},
    {LayerKind::Text,       "Text",       ContentKind::Text,        LayerRole::Rendered,   true},
    {LayerKind::Camera,     "Camera",     ContentKind::None,        LayerRole::Camera,     false},
    {LayerKind::Audio,      "Audio",      ContentKind::Audio,       LayerRole::Audio,      false},
}};

/// The hand-written predicate this work replaces, kept verbatim so the two can
/// be compared. `refresh/source.cpp:67` and `refresh/multi_source.cpp:28` are
/// byte-identical; `graph_builder_source_pass.cpp:72` shares the expression.
[[nodiscard]] constexpr bool legacy_layer_level_source(LayerKind kind) noexcept {
    return kind == LayerKind::Normal || kind == LayerKind::Shape || kind == LayerKind::Text;
}

/// `compiler/graph_builder/graph_builder_layer_pipeline.cpp:86` — "simple opaque
/// full-frame layer" (seeds the initial clear).
[[nodiscard]] constexpr bool legacy_simple_full_frame(LayerKind kind) noexcept {
    return kind == LayerKind::Normal || kind == LayerKind::Shape ||
           kind == LayerKind::Text || kind == LayerKind::Video;
}

/// `graph_builder_layer_pipeline.cpp:109` — mask-before-transform ordering.
[[nodiscard]] constexpr bool legacy_mask_before_transform(LayerKind kind) noexcept {
    return kind == LayerKind::Normal || kind == LayerKind::Shape ||
           kind == LayerKind::Text || kind == LayerKind::Precomp || kind == LayerKind::Video;
}

} // namespace

TEST_CASE("ADR-031 P1: every LayerKind maps to the signed-off (content, role) pair") {
    for (const Expectation& e : kTaxonomy) {
        CAPTURE(e.name);
        const ContentRef content = chronon3d::content_of(e.kind);
        CHECK(content.kind() == e.content);
        CHECK(chronon3d::role_of(e.kind) == e.role);
        CHECK(content.draws() == e.draws);
        CHECK(chronon3d::content_of(e.kind).draws() == e.draws);
    }
}

TEST_CASE("ADR-031 P1: Layer::content()/role()/carries_drawable_content() agree with the mapping") {
    for (const Expectation& e : kTaxonomy) {
        CAPTURE(e.name);
        Layer layer;
        layer.kind = e.kind;
        CHECK(layer.content().kind() == e.content);
        CHECK(layer.role() == e.role);
        CHECK(layer.carries_drawable_content() == e.draws);
    }
}

TEST_CASE("ADR-031 P1: carries_drawable_content() is a superset of every legacy chain") {
    // The canonical predicate must never be narrower than the chains it
    // replaces, otherwise P2 silently drops layers for the Video/Precomp paths.
    for (const Expectation& e : kTaxonomy) {
        CAPTURE(e.name);
        const bool canonical = e.draws;
        if (legacy_layer_level_source(e.kind)) {
            CHECK(canonical);
        }
        if (legacy_simple_full_frame(e.kind)) {
            CHECK(canonical);
        }
        if (legacy_mask_before_transform(e.kind)) {
            CHECK(canonical);
        }
    }
}

TEST_CASE("ADR-031 P1: the only non-drawing kinds are None-content and Audio") {
    for (const Expectation& e : kTaxonomy) {
        CAPTURE(e.name);
        const ContentRef content = chronon3d::content_of(e.kind);
        CHECK(content.draws() == (content.kind() != ContentKind::None &&
                                 content.kind() != ContentKind::Audio));
    }
}

TEST_CASE("ADR-031 P1: NoContent kinds are empty and carry no asset id") {
    for (LayerKind k : {LayerKind::Null, LayerKind::Adjustment, LayerKind::Camera}) {
        const ContentRef content = chronon3d::content_of(k);
        CHECK(content.empty());
        CHECK(content.kind() == ContentKind::None);
        CHECK(content.asset() == nullptr);
    }
}

TEST_CASE("ADR-031 P1: content-bearing kinds expose an asset id slot, unbound by default") {
    for (const Expectation& e : kTaxonomy) {
        if (e.content == ContentKind::None) {
            continue;
        }
        CAPTURE(e.name);
        const ContentRef content = chronon3d::content_of(e.kind);
        REQUIRE(content.asset() != nullptr);
        CHECK(*content.asset() == 0u); // "no asset bound yet"
    }
}

TEST_CASE("ADR-031 P1: a default Layer carries an unbound image, role Rendered") {
    Layer layer;
    CHECK(layer.kind == LayerKind::Normal);
    CHECK(layer.content().kind() == ContentKind::Image);
    CHECK_FALSE(layer.content().empty());
    CHECK(layer.role() == LayerRole::Rendered);
    CHECK(layer.carries_drawable_content());
}

TEST_CASE("ADR-031 P1: Audio and Null are roles, not content kinds") {
    Layer audio;
    audio.kind = LayerKind::Audio;
    CHECK(audio.content().kind() == ContentKind::Audio);
    CHECK_FALSE(audio.carries_drawable_content()); // timeline only, never a frame
    CHECK(audio.role() == LayerRole::Audio);

    Layer null_layer;
    null_layer.kind = LayerKind::Null;
    CHECK(null_layer.content().empty());
    CHECK(null_layer.role() == LayerRole::Null);
    CHECK_FALSE(null_layer.carries_drawable_content()); // pure transform parent
}

TEST_CASE("ADR-031 P1: Glass keeps the content axis of what it frosts") {
    Layer glass;
    glass.kind = LayerKind::Glass;
    CHECK(glass.content().kind() == ContentKind::Image);
    CHECK(glass.role() == LayerRole::Rendered);
    CHECK(glass.carries_drawable_content());
}

TEST_CASE("ADR-031 P1: the mapping is usable in constant expressions") {
    static_assert(chronon3d::content_of(LayerKind::Null).empty());
    static_assert(!chronon3d::content_of(LayerKind::Null).draws());
    static_assert(chronon3d::content_of(LayerKind::Text).kind() == ContentKind::Text);
    static_assert(chronon3d::content_of(LayerKind::Precomp).kind() == ContentKind::Composition);
    static_assert(chronon3d::role_of(LayerKind::Adjustment) == LayerRole::Adjustment);
    static_assert(chronon3d::role_of(LayerKind::Audio) == LayerRole::Audio);
    CHECK(true);
}
