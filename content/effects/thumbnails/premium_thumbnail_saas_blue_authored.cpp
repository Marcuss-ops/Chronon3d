// ═══════════════════════════════════════════════════════════════════════════
// premium_thumbnail_saas_blue_authored.cpp — PR 6 migration validation.
//
// Re-implements `PremiumThumbnailSaaSBlue` (defined in
// `premium_thumbnail_showcase.cpp`) using the new `chronon3d::authoring`
// DSL wherever the surface supports it, falling back to
// `Layer::configure_core([](LayerBuilder&){...})` for layer-level shape
// operations that the authoring surface does not yet expose
// (rect/path/rounded_rect/circle/star/glow/drop_shadow/opacity/position,
// and the high-level draw_rich_text helper).
//
// ── Surface completeness contract (PR 6) ───────────────────────────────
//
//   The chronon3d::authoring::Layer façade currently surfaces
//   `.text(content)` and `.configure_core(Fn)` ONLY.  Per-layer
//   LayerBuilder primitives (rect / path / glow / drop_shadow etc.) are
//   reachable in this migration ONLY through `configure_core([&](LayerBuilder&
//   core){...})`.  This is the documented surface boundary for PR 6
//   and is recorded in
//   `docs/migrations/2026-06-authoring-premium-thumbnail-migration.md`.
//
// ── Byte-equivalence validation protocol ──────────────────────────────
//
//   This composition is registered under the distinct name
//   `"PremiumThumbnailSaaSBlueAuthored"`.  The CLI render script
//   `tools/render_premium_artifacts.sh` (extended in PR 6) renders both
//   versions into separate directories, and `tools/compare_pngs.py`
//   asserts byte-identical frames.  The source composition must remain
//   untouched so frame 0 pixel diff identity is meaningful.
//
// ── Silent footgun preservation ───────────────────────────────────────
//
//   The brace-init source BUILDS a `TextMaterial mat = TextMaterial::premium()`
//   with rich gradient/bevel/glow/shadow settings inside `hero_text` layer
//   but NEVER assigns it into the `TextSpec` for `"saas_title"`.  We
//   intentionally mirror that quirk here — the migrated Layer::text("SAAS")
//   chain is constructed WITHOUT a `.material(...)` call so the rendered
//   pixel difference between brace-init and authored is exactly zero.
//   Adding `.material(...)` here would "fix" the bug and break
//   byte-equivalence, which is a desirable property for a validation
//   harness but a problematic property for production code.  Call out
//   the issue in `// NOTE:` comment here so reviewers know it isn't an
//   oversight.
// ═══════════════════════════════════════════════════════════════════════════

#include "../common/glow_test_common.hpp"
#include <chronon3d/layout/design_kit.hpp>
#include <chronon3d/presets/text/text_style_presets.hpp>
#include <chronon3d/authoring/authoring.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/text.hpp>

namespace chronon3d::content::effects {

namespace {

// Re-use the brace-init helper so the rich_text_line in
// draw_rich_text(FULL TUTORIAL) reproduces byte-for-byte.  Defined here
// instead of imported to keep the migration standalone (no header
// churn required).
[[nodiscard]] RichTextLine make_single_run_line(std::string text,
                                                Color color,
                                                f32 size,
                                                std::string font = "assets/fonts/Poppins-Bold.ttf") {
    RichTextLine line;
    line.run(std::move(text), color, size, std::move(font));
    return line;
}

} // namespace

Composition premium_thumbnail_saas_blue_authored() {
    return composition({.name = "PremiumThumbnailSaaSBlueAuthored", .width = kW, .height = kH, .duration = 1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const chronon3d::authoring::FrameContext authoring_ctx =
            chronon3d::authoring::FrameContext::from_dimensions((f32)kW, (f32)kH);

        // ── 1. background (radial gradient full-screen rect) ──────────
        s.layer("background", [&](LayerBuilder& builder) {
            chronon3d::authoring::Layer l(builder, authoring_ctx);
            l.configure_core([](LayerBuilder& core) {
                core.rect("bg", {
                    .size = {(f32)kW, (f32)kH},
                    .color = Color::white(),
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::radial(
                        {0.5f, 0.5f},
                        0.80f,
                        {
                            {0.0f, Color{0.015f, 0.18f, 0.48f, 1.0f}}, // Bright cyan-blue center spotlight
                            {0.55f, Color{0.003f, 0.035f, 0.12f, 1.0f}}, // Dark blue
                            {1.0f, Color{0.001f, 0.004f, 0.02f, 1.0f}} // Vignette corners
                        }
                    )
                });
            });
        });

        // ── 2. light_beam (volumetric triangle path) ─────────────────
        s.layer("light_beam", [&](LayerBuilder& builder) {
            chronon3d::authoring::Layer l(builder, authoring_ctx);
            l.configure_core([](LayerBuilder& core) {
                core.position({0.0f, 0.0f, 0.0f});
                core.opacity(0.85f);

                PathParams p;
                p.commands = {
                    PathCommand::move_to({0.0f, -512.0f}),
                    PathCommand::line_to({-768.0f, 512.0f}),
                    PathCommand::line_to({768.0f, 512.0f}),
                    PathCommand::close()
                };
                p.fill = Fill::linear(
                    {0.5f, 0.0f},
                    {0.5f, 1.0f},
                    {
                        {0.0f, Color{0.0f, 0.85f, 1.0f, 0.35f}},
                        {1.0f, Color{0.005f, 0.040f, 0.180f, 0.00f}}
                    }
                );
                core.path("beam", p);
            });
        });

        // ── 3. badge (rounded_rect card × 2 + text "Ae") ────────────
        s.layer("badge", [&](LayerBuilder& builder) {
            chronon3d::authoring::Layer l(builder, authoring_ctx);
            l.configure_core([](LayerBuilder& core) {
                core.position({0.0f, -250.0f, 0.0f});
                core.glow(GlowParams{.radius = 20.0f, .intensity = 0.80f, .color = Color{0.0f, 0.80f, 1.0f, 0.60f}});
                core.drop_shadow({0.0f, 10.0f}, Color{0.0f, 0.0f, 0.0f, 0.50f}, 14.0f);

                // Outer border card
                core.rounded_rect("ae_card_border", {
                    .size = {1280.0f * 0.1f, 1280.0f * 0.1f},
                    .radius = 26.0f,
                    .color = Color{0.0f, 0.85f, 1.0f, 0.85f}
                });

                // Inner filled card
                core.rounded_rect("ae_card", {
                    .size = {120.0f, 120.0f},
                    .radius = 23.0f,
                    .color = Color::white(),
                    .fill = FillStyle::linear(
                        {0.0f, 0.0f},
                        {0.0f, 1.0f},
                        {
                            {0.0f, Color{0.08f, 0.12f, 0.32f, 1.0f}},
                            {1.0f, Color{0.01f, 0.02f, 0.08f, 1.0f}},
                        }
                    )
                });
            });

            // Authoring-DSL text replacement for l.text("ae_text", TextSpec{...})
            l.text("Ae")
             .id("ae_text")
             .font("assets/fonts/Poppins-Bold.ttf", 48.0f)
             .font_family("Inter")
             .weight(800)
             .box({120.0f, 120.0f})
             .align(TextAlign::Center)
             .vertical_align(VerticalAlign::Middle)
             .color(Color{0.0f, 0.85f, 1.0f, 1.0f})
             .at(0.0f, 0.0f);
        });

        // ── 4. sparkles (2 stars) ─────────────────────────────────
        s.layer("sparkles", [&](LayerBuilder& builder) {
            chronon3d::authoring::Layer l(builder, authoring_ctx);
            l.configure_core([](LayerBuilder& core) {
                core.glow(GlowParams{.radius = 18.0f, .intensity = 0.95f, .color = Color::white()});

                core.star("star_left", {
                    .center = {-340.0f, -50.0f},
                    .points = 4,
                    .inner_radius = 8.0f,
                    .outer_radius = 35.0f,
                    .color = Color::white()
                });

                core.star("star_right", {
                    .center = {340.0f, 50.0f},
                    .points = 4,
                    .inner_radius = 10.0f,
                    .outer_radius = 45.0f,
                    .color = Color::white()
                });
            });
        });

        // ── 5. hero_text (position + SAAS text + rich-text FULL TUTORIAL) ──
        s.layer("hero_text", [&](LayerBuilder& builder) {
            chronon3d::authoring::Layer l(builder, authoring_ctx);

            l.configure_core([](LayerBuilder& core) {
                core.position({0.0f, -20.0f, 0.0f}); // Moved up in layout
            });

            // Authoring-DSL text replacement for
            //   l.text("saas_title", TextSpec{
            //       .content = {.value = "SAAS"},
            //       .font    = {.font_path = "Poppins-Bold", .font_family = "Inter", .font_weight = 800, .font_size = 110.0f},
            //       .layout  = {.box = {960.0f, 260.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .wrap = TextWrap::None},
            //       .appearance = {.color = Color{0.98f, 1.0f, 1.0f, 1.0f}},
            //       .position = {0.0f, 0.0f, 0.0f}
            //   });
            //
            // ── NOTE: source builds `TextMaterial mat = TextMaterial::premium()`
            //   with rich gradient/bevel/glow/shadow settings inside this
            //   layer but NEVER assigns it into the TextSpec for
            //   "saas_title".  Reflected by chaining NO `.material(...)`
            //   call below — adding one would "fix" the bug and break
            //   byte-equivalence, which is undesirable for a validation
            //   harness even though it would be a real bugfix in
            //   production.  Discriminated in the migration doc's
            //   "Lessons learned" section.
            l.text("SAAS")
             .id("saas_title")
             .font("assets/fonts/Poppins-Bold.ttf", 110.0f)
             .font_family("Inter")
             .weight(800)
             .box({960.0f, 260.0f})
             .align(TextAlign::Center)
             .vertical_align(VerticalAlign::Middle)
             .wrap(TextWrap::None)
             .color(Color{0.98f, 1.0f, 1.0f, 1.0f})
             .at(0.0f, 0.0f);

            // Authoring surface does not yet expose the
            // draw_rich_text(l, line, pos, opts, font_engine, name) helper.
            // Reproduce via configure_core for byte-exact parity.  This
            // is documented in
            // docs/migrations/2026-06-authoring-premium-thumbnail-migration.md
            // §"Surface boundary".
            l.configure_core([&builder](LayerBuilder& core) {
                auto sub_style = presets::text::premium_subtitle();
                sub_style.size = 36.0f;
                sub_style.tracking = 16.0f;
                sub_style.color = Color{0.85f, 0.93f, 1.0f, 1.0f};
                sub_style.paint.fill = sub_style.color;
                sub_style.paint.fill_style = std::nullopt;
                sub_style.paint.stroke_enabled = false;
                sub_style.material = TextMaterial::glass();
                sub_style.material.top_color = {0.95f, 0.98f, 1.0f, 1.0f};
                sub_style.material.bottom_color = {0.75f, 0.88f, 1.0f, 1.0f};
                RichTextLine subtitle = make_single_run_line("FULL TUTORIAL", sub_style.color, sub_style.size);
                subtitle.tracking(sub_style.tracking).paint(sub_style.paint).material(sub_style.material);
                draw_rich_text(
                    core,
                    subtitle,
                    {0.0f, 160.0f, 0.0f},
                    {
                        .anchor = RichTextAnchor::Center,
                        .vertical_anchor = RichTextVerticalAnchor::Middle,
                        .glyph_padding = 4.0f,
                        .snap_to_pixels = true,
                        .max_width = 1240.0f,
                        .fit_to_width = true,
                    },
                    core.font_engine(),
                    "saas_sub"
                );
                // suppress unused-variable warning on borrowed builder ref
                (void)builder;
            });
        });

        // ── 6. arc (glowing curved horizon line) ─────────────────────
        s.layer("arc", [&](LayerBuilder& builder) {
            chronon3d::authoring::Layer l(builder, authoring_ctx);
            l.configure_core([](LayerBuilder& core) {
                core.position({0.0f, 512.0f, 0.0f}); // Placed at the bottom edge
                core.glow(GlowParams{.radius = 35.0f, .intensity = 1.6f, .color = Color{0.0f, 0.85f, 1.0f, 1.0f}});

                PathParams p;
                p.commands = {
                    PathCommand::move_to({-768.0f, 0.0f}),
                    PathCommand::quadratic_to({0.0f, -60.0f}, {768.0f, 0.0f}),
                    PathCommand::line_to({768.0f, 20.0f}),
                    PathCommand::line_to({-768.0f, 20.0f}),
                    PathCommand::close()
                };
                p.fill = Fill::linear(
                    {0.5f, 0.0f},
                    {0.5f, 1.0f},
                    {
                        {0.0f, Color{0.0f, 0.90f, 1.0f, 0.75f}},
                        {1.0f, Color{0.00f, 0.35f, 1.0f, 0.00f}}
                    }
                );
                core.path("glow_curve", p);
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::effects
