// The DirectYUV text lane has ONE classifier: the canonical text contract.
// It decides whether a prepared run's layout/MTSDF contract is established,
// whether the remaining style semantics lower faithfully, and exactly which
// single drop shadow the CUDA kernel resamples from that prepared distance
// field. These cases pin the rules the declared capability matrix consumes, so
// a shadow stack the kernel cannot represent can never be silently
// approximated on the zero-conversion lane.

#include <doctest/doctest.h>

#include "src/media/video/direct_yuv/direct_yuv_text_contract.hpp"

#include <limits>
#include <memory>
#include <string>

using chronon3d::Color;
using chronon3d::ShapedFontSpan;
using chronon3d::TextAnimatorSpec;
using chronon3d::TextRunLayout;
using chronon3d::TextRunShape;
using chronon3d::TextShadow;
using chronon3d::media::video::resolve_direct_yuv_text_contract;

namespace {

// Minimal prepared run: an immutable layout carrying one glyph and one font
// span is exactly what the MTSDF lowering requires, so the style semantics are
// the only variable under test.
TextRunShape prepared_run() {
    TextRunShape shape;
    auto layout = std::make_shared<TextRunLayout>();
    layout->source_text = "THIS CHANGES EVERYTHING";
    layout->font.font_path = "/fonts/Inter-Bold.ttf";
    layout->font_size = 64.0f;
    layout->placed.glyphs.resize(1);
    layout->placed.glyphs[0].glyph_id = 42;
    ShapedFontSpan span;
    span.glyph_begin = 0;
    span.glyph_end = 1;
    layout->font_spans.push_back(span);
    shape.layout = layout;
    return shape;
}

// The canonical overlay preset shadow every official text preset carries.
TextShadow canonical_shadow() {
    TextShadow shadow;
    shadow.enabled = true;
    shadow.offset = {0.0f, 4.0f};
    shadow.blur = 12.0f;
    shadow.opacity = 0.72f;
    shadow.color = Color{0.0f, 0.0f, 0.0f, 1.0f};
    return shadow;
}

std::string rejection_of(const TextRunShape& shape) {
    const auto contract = resolve_direct_yuv_text_contract(shape);
    return contract.rejection ? std::string(contract.rejection) : std::string();
}

} // namespace

TEST_CASE("DirectYUV text contract: a run without a canonical layout is refused") {
    const TextRunShape empty;
    const auto contract = resolve_direct_yuv_text_contract(empty);
    CHECK(rejection_of(empty) == "text_missing_canonical_layout");
    CHECK_FALSE(contract.prepared_mtsdf);
    CHECK_FALSE(contract.style_representable);
}

TEST_CASE("DirectYUV text contract: the MTSDF layout contract is explicit") {
    TextRunShape no_glyphs = prepared_run();
    auto layout = std::make_shared<TextRunLayout>(*no_glyphs.layout);
    layout->placed.glyphs.clear();
    no_glyphs.layout = layout;
    CHECK(rejection_of(no_glyphs) == "text_layout_has_no_glyphs");

    TextRunShape no_spans = prepared_run();
    auto spans_layout = std::make_shared<TextRunLayout>(*no_spans.layout);
    spans_layout->font_spans.clear();
    no_spans.layout = spans_layout;
    CHECK(rejection_of(no_spans) == "text_missing_font_spans");
}

TEST_CASE("DirectYUV text contract: a plain prepared run is representable") {
    const TextRunShape shape = prepared_run();
    const auto contract = resolve_direct_yuv_text_contract(shape);
    CHECK(contract.rejection == nullptr);
    CHECK(contract.prepared_mtsdf);
    CHECK(contract.style_representable);
    CHECK_FALSE(contract.shadow.enabled);
}

TEST_CASE("DirectYUV text contract: one canonical shadow lowers faithfully") {
    TextRunShape shape = prepared_run();
    shape.shadows.push_back(canonical_shadow());

    const auto contract = resolve_direct_yuv_text_contract(shape);
    CHECK(contract.rejection == nullptr);
    CHECK(contract.prepared_mtsdf);
    CHECK(contract.style_representable);
    REQUIRE(contract.shadow.enabled);
    CHECK(contract.shadow.offset_x == doctest::Approx(0.0f));
    CHECK(contract.shadow.offset_y == doctest::Approx(4.0f));
    CHECK(contract.shadow.blur == doctest::Approx(12.0f));
    // Opacity stays its own factor; the canonical text renderer applies it once
    // to the soft coverage instead of baking it into the colour alpha.
    CHECK(contract.shadow.opacity == doctest::Approx(0.72f));
    CHECK(contract.shadow.color.a == doctest::Approx(1.0f));
}

TEST_CASE("DirectYUV text contract: a disabled shadow is not a shadow") {
    TextRunShape shape = prepared_run();
    TextShadow disabled = canonical_shadow();
    disabled.enabled = false;
    shape.shadows.push_back(disabled);

    const auto contract = resolve_direct_yuv_text_contract(shape);
    CHECK(contract.rejection == nullptr);
    CHECK(contract.style_representable);
    CHECK_FALSE(contract.shadow.enabled);
}

TEST_CASE("DirectYUV text contract: a transparent shadow does not force a fallback") {
    TextRunShape shape = prepared_run();
    TextShadow transparent = canonical_shadow();
    transparent.opacity = 0.0f;
    shape.shadows.push_back(transparent);

    const auto contract = resolve_direct_yuv_text_contract(shape);
    CHECK(contract.rejection == nullptr);
    CHECK(contract.style_representable);
    CHECK_FALSE(contract.shadow.enabled);
}

TEST_CASE("DirectYUV text contract: multiple enabled shadows stay a fallback") {
    TextRunShape shape = prepared_run();
    shape.shadows.push_back(canonical_shadow());
    shape.shadows.push_back(canonical_shadow());

    const auto contract = resolve_direct_yuv_text_contract(shape);
    CHECK(rejection_of(shape) == "text_multiple_shadows_unsupported");
    // The prepared MTSDF contract still holds; it is the STYLE that cannot be
    // represented by a single distance-field resample.
    CHECK(contract.prepared_mtsdf);
    CHECK_FALSE(contract.style_representable);
}

TEST_CASE("DirectYUV text contract: an invalid shadow is refused, never clamped") {
    for (const float bad_blur : {-1.0f, std::numeric_limits<float>::quiet_NaN()}) {
        TextRunShape shape = prepared_run();
        TextShadow shadow = canonical_shadow();
        shadow.blur = bad_blur;
        shape.shadows.push_back(shadow);
        CHECK(rejection_of(shape) == "text_shadow_invalid");
    }

    TextRunShape nan_offset = prepared_run();
    TextShadow shadow = canonical_shadow();
    shadow.offset = {std::numeric_limits<float>::quiet_NaN(), 0.0f};
    nan_offset.shadows.push_back(shadow);
    CHECK(rejection_of(nan_offset) == "text_shadow_invalid");
}

TEST_CASE("DirectYUV text contract: richer text semantics stay explicit fallbacks") {
    TextRunShape animated = prepared_run();
    TextAnimatorSpec animator;
    animator.enabled = true;
    animated.animators.push_back(animator);
    CHECK(rejection_of(animated) == "text_glyph_animation_unsupported");

    TextRunShape gradient = prepared_run();
    gradient.paint.fill_style = chronon3d::Fill::linear(
        {0.0f, 0.0f}, {0.0f, 1.0f},
        {{0.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}},
         {1.0f, Color{0.0f, 0.0f, 0.0f, 1.0f}}});
    CHECK(rejection_of(gradient) == "text_gradient_paint_unsupported");

    TextRunShape material = prepared_run();
    material.material.enabled = true;
    CHECK(rejection_of(material) == "text_material_unsupported");
}

TEST_CASE("DirectYUV text contract: a refused style keeps the MTSDF contract") {
    TextRunShape shape = prepared_run();
    shape.shadows.push_back(canonical_shadow());
    shape.shadows.push_back(canonical_shadow());

    const auto contract = resolve_direct_yuv_text_contract(shape);
    CHECK(contract.prepared_mtsdf);
    CHECK_FALSE(contract.style_representable);

    // A run refused BEFORE the MTSDF contract is established reports both
    // flags as false, so the matrix can never claim a prepared field it does
    // not have.
    TextRunShape no_layout;
    const auto unestablised = resolve_direct_yuv_text_contract(no_layout);
    CHECK_FALSE(unestablised.prepared_mtsdf);
    CHECK_FALSE(unestablised.style_representable);
}
