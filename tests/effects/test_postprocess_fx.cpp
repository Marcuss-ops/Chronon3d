// ---------------------------------------------------------------------------
// test_postprocess_fx.cpp — Post-process FX family (V1) acceptance tests.
// See docs/tickets/TICKET-POSTPROCESS-FX.md for the shader-math contracts.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/effects/effect_type.hpp>
#include <chronon3d/render_graph/effect_gpu_capability.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include "processors/effects/postprocess/postprocess_fx.hpp"

#include <cmath>
#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::effects;
namespace effect_ids = chronon3d::effects::ids;
using namespace chronon3d::renderer;

namespace {

constexpr float kEps = 1e-4f;

void paint_square(Framebuffer& fb, int x0, int y0, int x1, int y1,
                  const Color& color) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            fb.set_pixel(x, y, color);
}

} // namespace

TEST_CASE("Post-process FX: catalog registration") {
    EffectCatalog catalog;
    CHECK(catalog.contains(effect_ids::StylizeScanlines));
    CHECK(catalog.contains(effect_ids::StylizeRetroTv));
    CHECK(catalog.contains(effect_ids::LightHalation));
    CHECK(catalog.contains(effect_ids::LightAnamorphicStreak));
    CHECK(catalog.contains(effect_ids::ColorPosterize));

    static_assert(static_cast<std::uint8_t>(EffectType::Scanlines) == 49);
    static_assert(static_cast<std::uint8_t>(EffectType::RetroTv) == 50);
    static_assert(static_cast<std::uint8_t>(EffectType::Halation) == 51);
    static_assert(static_cast<std::uint8_t>(EffectType::AnamorphicStreak) == 52);

    CHECK(effect_type_for<ScanlinesParams>::value == EffectType::Scanlines);
    CHECK(effect_type_for<RetroTvParams>::value == EffectType::RetroTv);
    CHECK(effect_type_for<HalationParams>::value == EffectType::Halation);
    CHECK(effect_type_for<AnamorphicStreakParams>::value == EffectType::AnamorphicStreak);

    CHECK((std::is_same_v<effect_params_for<EffectType::Scanlines>::type, ScanlinesParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::RetroTv>::type, RetroTvParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Halation>::type, HalationParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::AnamorphicStreak>::type, AnamorphicStreakParams>));
}

TEST_CASE("Post-process FX: parameters validate their supported ranges") {
    SUBCASE("Scanlines rejects bad spacing") {
        ScanlinesParams p;
        p.spacing_px = 0.5f;
        CHECK_THROWS_AS(p.validate(), std::invalid_argument);
    }
    SUBCASE("RetroTv rejects out-of-range jitter") {
        RetroTvParams p;
        p.jitter_px = 64.0f;
        CHECK_THROWS_AS(p.validate(), std::invalid_argument);
    }
    SUBCASE("Halation rejects out-of-range radius") {
        HalationParams p;
        p.radius_px = 65.0f;
        CHECK_THROWS_AS(p.validate(), std::invalid_argument);
    }
    SUBCASE("AnamorphicStreak rejects out-of-range length") {
        AnamorphicStreakParams p;
        p.streak_length_px = 600.0f;
        CHECK_THROWS_AS(p.validate(), std::invalid_argument);
    }
    SUBCASE("Defaults validate") {
        ScanlinesParams{}.validate();
        RetroTvParams{}.validate();
        HalationParams{}.validate();
        AnamorphicStreakParams{}.validate();
    }
}

TEST_CASE("Post-process FX: scanlines modulate luminance along the line axis") {
    Framebuffer fb(32, 32);
    fb.clear(Color{0.5f, 0.5f, 0.5f, 1.0f});
    const Framebuffer copy = fb;

    ScanlinesParams p;
    p.spacing_px = 2.0f;
    p.amount = 0.25f;
    apply_scanlines(fb, p);

    // With a 2 px pitch the (y + 0.15) triangle wave puts y=0 near a dip and
    // y=1 near a crest, so neighbouring rows must differ once modulated.
    const Color row_a = fb.get_pixel(16, 0);
    const Color row_b = fb.get_pixel(16, 1);
    CHECK(row_a.r < copy.get_pixel(16, 0).r);
    CHECK(row_b.r == doctest::Approx(copy.get_pixel(16, 1).r).epsilon(1e-3));
    CHECK(row_b.r > row_a.r);

    SUBCASE("alpha is preserved") {
        CHECK(fb.get_pixel(5, 5).a == doctest::Approx(1.0f).epsilon(kEps));
    }
    SUBCASE("amount=0 is identity") {
        Framebuffer flat(32, 32);
        flat.clear(Color{0.5f, 0.5f, 0.5f, 1.0f});
        ScanlinesParams zero;
        zero.amount = 0.0f;
        apply_scanlines(flat, zero);
        for (int y = 0; y < 32; y += 7)
            for (int x = 0; x < 32; x += 7)
                CHECK(flat.get_pixel(x, y).r ==
                      doctest::Approx(0.5f).epsilon(kEps));
    }
}

TEST_CASE("Post-process FX: retro TV is deterministic and jitterable") {
    EffectScratchResources scratch;
    Framebuffer fb(64, 24);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 64; ++x)
            fb.set_pixel(x, y, Color{0.25f + 0.008f * x, 0.2f, 0.3f, 1.0f});

    SUBCASE("zero-strength settings reproduce the source") {
        Framebuffer copy = fb;
        RetroTvParams p;
        p.jitter_px = 0.0f;
        p.smear_px = 0.0f;
        p.luma_noise = 0.0f;
        apply_retro_tv(fb, p, 1.0f, scratch);
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 64; ++x)
                CHECK(fb.get_pixel(x, y).r ==
                      doctest::Approx(copy.get_pixel(x, y).r).epsilon(kEps));
    }

    SUBCASE("jitter shifts rows horizontally and is stable for identical inputs") {
        Framebuffer a = fb;
        Framebuffer b = fb;
        RetroTvParams p;
        p.jitter_px = 16.0f;
        p.jitter_speed = 4.0f;
        apply_retro_tv(a, p, 0.5f, scratch);
        apply_retro_tv(b, p, 0.5f, scratch);
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 64; ++x)
                CHECK(a.get_pixel(x, y).r ==
                      doctest::Approx(b.get_pixel(x, y).r).epsilon(kEps));

        Framebuffer moved = a;
        // Advance deterministic time by one tape quantum: the band hash must
        // change, so at least one row misaligns with the un-jittered source.
        Framebuffer copy = fb;
        apply_retro_tv(moved, p, 0.75f, scratch);
        int differing_rows = 0;
        for (int y = 0; y < 24; ++y) {
            if (moved.get_pixel(32, y).r !=
                doctest::Approx(copy.get_pixel(32, y).r).epsilon(kEps))
                ++differing_rows;
        }
        CHECK(differing_rows > 0);
    }

    SUBCASE("chroma smear low-passes horizontal detail without NaNs") {
            Framebuffer fb2 = fb;
            RetroTvParams p;
            p.jitter_px = 0.0f;
            p.smear_px = 6.0f;
            p.bleed_strength = 1.0f;
            p.luma_noise = 0.0f;
            apply_retro_tv(fb2, p, 2.0f, scratch);
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 64; ++x)
                    CHECK(std::isfinite(fb2.get_pixel(x, y).r));
    }
}

TEST_CASE("Post-process FX: halation brightens around highlights only") {
    EffectScratchResources scratch;
    Framebuffer fb(48, 48);
    fb.clear(Color{0.05f, 0.05f, 0.05f, 1.0f});
    paint_square(fb, 20, 20, 27, 27, Color{1.0f, 1.0f, 1.0f, 1.0f});

    const Color dark_before = fb.get_pixel(8, 24);
    const Color edge_before = fb.get_pixel(29, 24);

    HalationParams p;
    p.threshold = 0.85f;
    p.softness = 0.1f;
    p.radius_px = 6.0f;
    p.intensity = 1.0f;
    apply_halation(fb, p, scratch);

    const Color edge_after = fb.get_pixel(29, 24);
    const Color dark_after = fb.get_pixel(8, 24);

    // Pixels just outside the bright square receive warm bleed.
    CHECK(edge_after.r > edge_before.r);
    CHECK(edge_after.g > edge_before.g);
    // Far dark field stays dark (bleed decays within the blur radius).
    CHECK(dark_after.r == doctest::Approx(dark_before.r).epsilon(kEps));

    SUBCASE("sub-threshold image is untouched") {
        Framebuffer flat(32, 32);
        flat.clear(Color{0.2f, 0.2f, 0.2f, 1.0f});
        const Framebuffer copy = flat;
        HalationParams off;
        off.threshold = 0.85f;
        apply_halation(flat, off, scratch);
        for (int y = 0; y < 32; y += 5)
            for (int x = 0; x < 32; x += 5)
                CHECK(flat.get_pixel(x, y).r ==
                      doctest::Approx(copy.get_pixel(x, y).r).epsilon(kEps));
    }
}

TEST_CASE("Post-process FX: anamorphic streak elongates highlights along X") {
    EffectScratchResources scratch;
    Framebuffer fb(64, 64);
    fb.clear(Color{0.02f, 0.02f, 0.02f, 1.0f});
    paint_square(fb, 30, 30, 33, 33, Color{1.0f, 1.0f, 1.0f, 1.0f});

    const Color row_left_before = fb.get_pixel(16, 31);
    const Color fell_before = fb.get_pixel(31, 12);

    AnamorphicStreakParams p;
    p.threshold = 0.85f;
    p.softness = 0.1f;
    p.streak_length_px = 40.0f;
    p.intensity = 2.0f;
    apply_anamorphic_streak(fb, p, scratch);

    const Color row_left_after = fb.get_pixel(16, 31);
    const Color fell_after = fb.get_pixel(31, 12);

    // Same row as the highlight, far to the left: streaked.
    CHECK(row_left_after.r > row_left_before.r);
    // Same column, far above: untouched (streak is horizontal-only).
    CHECK(fell_after.r == doctest::Approx(fell_before.r).epsilon(kEps));

    SUBCASE("streak does not exceed the intensity budget") {
        CHECK(fb.get_pixel(16, 31).r <= 1.0f + kEps);
    }
}

TEST_CASE("Post-process FX: GPU capability rows fail closed without crashing") {
    using effects::EffectType;
    for (const EffectType type : {EffectType::Scanlines, EffectType::RetroTv,
                                  EffectType::Halation, EffectType::AnamorphicStreak}) {
        const auto* row = chronon3d::graph::gpu_effect_capability(type);
        REQUIRE(row != nullptr);
        // Post-process FX V1 ships CPU-native routes only; the strict Vulkan
        // native policy fails closed (SoftwareCapability::Unsupported) until a
        // certified compute-kernel batch lands.
        CHECK(row->route == chronon3d::graph::GpuEffectRoute::Unsupported);
        CHECK_FALSE(row->diagnostic.empty());
    }
}

TEST_CASE("Post-process FX: animatable parameter whitelist") {
    ScanlinesParams scanlines;
    CHECK(set_animatable_effect_param(scanlines, effect_ids::StylizeScanlines,
                                      "amount", 0.12f));
    CHECK(scanlines.amount == doctest::Approx(0.12f).epsilon(kEps));

    HalationParams halation;
    CHECK(set_animatable_effect_param(halation, effect_ids::LightHalation,
                                      "threshold", 0.6f));
    CHECK(halation.threshold == doctest::Approx(0.6f).epsilon(kEps));
    CHECK_FALSE(set_animatable_effect_param(halation, effect_ids::LightHalation,
                                            "not_a_param", 1.0f));
}
