#include <chronon3d/chronon3d.hpp>
#include <fmt/format.h>

using namespace chronon3d;

// ── Shared helpers ────────────────────────────────────────────────────────────

static constexpr int   kW = 1080;
static constexpr int   kH = 1080;
static const Color kBg{0.051f, 0.067f, 0.090f, 1.0f};         // #0D1117
static const Color kGrid{1.0f, 1.0f, 1.0f, 0.055f};
static const Color kTitle{1.0f, 1.0f, 1.0f, 0.90f};
static const Color kCheck{0.60f, 0.70f, 0.80f, 0.85f};
static const Color kSep{1.0f, 1.0f, 1.0f, 0.20f};

static const std::string kBoldFont    = "assets/fonts/Inter-Bold.ttf";
static const std::string kRegFont     = "assets/fonts/Inter-Regular.ttf";

// Draw the common scaffold: background, grid, header label, separator, footer.
static void draw_scaffold(SceneBuilder& s,
                           const std::string& label,
                           const std::string& check_note)
{
    // Background
    s.rect("bg", { .size = {(float)kW, (float)kH},
                   .color = kBg, .pos = {kW/2.f, kH/2.f, -1} });

    // Subtle grid (100px spacing)
    const int step = 100;
    for (int x = 0; x <= kW; x += step) {
        s.rect(fmt::format("gx{}", x), {
            .size = {1.f, (float)kH}, .color = kGrid,
            .pos  = {(float)x, kH/2.f, -0.5f}
        });
    }
    for (int y = 0; y <= kH; y += step) {
        s.rect(fmt::format("gy{}", y), {
            .size = {(float)kW, 1.f}, .color = kGrid,
            .pos  = {kW/2.f, (float)y, -0.5f}
        });
    }

    // Title label
    s.text("title_label", {
        .content = label,
        .style   = {kBoldFont, 22.0f, kTitle},
        .pos     = {40, 38, 0}
    });

    // Separator under title
    s.rect("sep", { .size = {(float)(kW - 40), 1.f}, .color = kSep,
                    .pos  = {kW/2.f + 20, 82, 0} });

    // Footer check note
    s.text("check", {
        .content = check_note,
        .style   = {kRegFont, 18.0f, kCheck},
        .pos     = {75, 820, 0}
    });
}

// Horizontal guide line helper.
static void hline(SceneBuilder& s, const std::string& id, float y, Color col) {
    s.rect(id, { .size = {(float)(kW - 80), 1.f}, .color = col,
                 .pos  = {kW/2.f, y, 0.2f} });
}

// ── 01  Baseline / Ascenders / Descenders ────────────────────────────────────

// Inter Bold at 120px: ascent≈95px, cap-height≈70px, descent≈25px (below baseline).
// Text top placed at y=350 → baseline @ 350+95=445 → cap-line @ 445-70=375.
static const float kFontSize01 = 120.f;
static const float kTextTopY   = 350.f;
static const float kBaseline   = kTextTopY + 95.f;   // 445
static const float kCapLine    = kBaseline - 70.f;    // 375
static const float kDescLine   = kBaseline + 25.f;    // 470
Composition TextGlyph01_BaselineAscendersDescenders() {
    return composition({
        .name = "TextGlyph01_BaselineAscendersDescenders",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "01_baseline_ascenders_descenders",
                      "Check: baseline, accents, descenders must not be clipped.");

        // Guide lines (draw before text so text renders on top)
        hline(s, "capline",  kCapLine,  {0.40f, 0.60f, 1.00f, 0.75f}); // blue
        hline(s, "baseline", kBaseline, {1.00f, 0.30f, 0.30f, 0.80f}); // red
        hline(s, "descline", kDescLine, {1.00f, 0.60f, 0.20f, 0.75f}); // orange

        // Main test text (center-aligned, top at kTextTopY)
        s.text("main", {
            .content = "Hgjpqy \xc3\x81\xc3\x89\xc3\x8d\xc3\x93\xc3\x9a",  // "Hgjpqy ÁÉÍÓÚ"
            .style   = {kBoldFont, kFontSize01, Color{1, 1, 1, 1}, TextAlign::Center},
            .pos     = {kW/2.f, kTextTopY, 0.5f}
        });

        return s.build();
    });
}

// ── 02  Kerning Pairs ─────────────────────────────────────────────────────────
Composition TextGlyph02_KerningPairs() {
    return composition({
        .name = "TextGlyph02_KerningPairs",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "02_kerning_pairs",
                      "Check: AV/To/Yo spacing should look natural, not monospaced.");

        const TextStyle st{kRegFont, 80.f, Color{1, 1, 1, 1}};
        const float x = 100.f;
        float y = 175.f;
        const float step = 125.f;

        for (auto [id, text] : std::initializer_list<std::pair<const char*, const char*>>{
            {"r1", "AVAVA"},
            {"r2", "ToToTo"},
            {"r3", "WA WA"},
            {"r4", "YoYo"},
            {"r5", "Ta Va Ly"}
        }) {
            s.text(id, { .content = text, .style = st, .pos = {x, y, 0} });
            y += step;
        }

        return s.build();
    });
}

// ── 03  Glyph Coverage: Accents & Symbols ────────────────────────────────────
Composition TextGlyph03_GlyphCoverageAccentsSymbols() {
    return composition({
        .name = "TextGlyph03_GlyphCoverageAccentsSymbols",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "03_glyph_coverage_accents_symbols",
                      "Check: unsupported glyphs should use clean fallback, not tofu boxes.");

        const TextStyle st{kRegFont, 48.f, Color{1, 1, 1, 1}};
        const float x = 75.f;
        float y = 150.f;

        // Row 1: lowercase accented
        s.text("r1", {
            .content = "\xc3\xa0 \xc3\xa8 \xc3\xa9 \xc3\xac \xc3\xb2 \xc3\xb9 "
                       "\xc3\xa7 \xc3\xb1 \xc3\xbc \xc3\x9f \xc3\xb8 \xc3\xa5 \xc3\xa6 \xc5\x93",
            .style = st, .pos = {x, y, 0}
        });
        y += 110;

        // Row 2: uppercase accented
        s.text("r2", {
            .content = "\xc3\x81 \xc3\x89 \xc3\x8d \xc3\x93 \xc3\x9a "
                       "\xc3\x87 \xc3\x91 \xc3\x9c",
            .style = st, .pos = {x, y, 0}
        });
        y += 110;

        // Row 3: digits + currency + symbols
        s.text("r3", {
            .content = "0123456789 \xe2\x82\xac \xc2\xa3 $ % & @ #",
            .style = st, .pos = {x, y, 0}
        });
        y += 110;

        // Row 4: punctuation / curly quotes
        s.text("r4", {
            .content = ".,;:!? \xe2\x80\x9c""quotes\xe2\x80\x9d \xe2\x80\x98""apostrophes\xe2\x80\x99",
            .style = st, .pos = {x, y, 0}
        });
        y += 110;

        // Row 5: arrows + math
        s.text("r5", {
            .content = "arrows \xe2\x86\x90 \xe2\x86\x91 \xe2\x86\x92 \xe2\x86\x93  "
                       "math \xc2\xb1 \xc3\x97 \xc3\xb7 \xe2\x89\xa0 \xe2\x89\xa4 \xe2\x89\xa5",
            .style = st, .pos = {x, y, 0}
        });

        return s.build();
    });
}

// ── 04  Multiline Wrap & Alignment ───────────────────────────────────────────
Composition TextGlyph04_MultilineWrapAlignment() {
    return composition({
        .name = "TextGlyph04_MultilineWrapAlignment",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "04_multiline_wrap_alignment",
                      "Check: line breaks, centered alignment, max width, no clipping.");

        // Card border
        s.rounded_rect("card_bg", {
            .size   = {840, 580},
            .radius = 26,
            .color  = Color{0.10f, 0.13f, 0.22f, 1.0f},
            .pos    = {kW/2.f, 480, 0}
        });
        s.rounded_rect("card_border", {
            .size   = {838, 578},
            .radius = 26,
            .color  = Color{0.30f, 0.40f, 0.70f, 0.55f},
            .pos    = {kW/2.f, 480, 0.1f}
        });

        // Wrapped centered text inside card (pos.x = center for TextAlign::Center)
        s.text("wrap_text", {
            .content = "This is a long headline that should wrap cleanly "
                       "inside the text box without clipping letters like g, y, p, q "
                       "or accented chars.",
            .style   = {kBoldFont, 52.f, Color{1, 1, 1, 1}, TextAlign::Center},
            .pos     = {kW/2.f, 215, 0.2f},
            .box     = {.size = {780, 580}, .enabled = true}
        });

        return s.build();
    });
}

// ── 05  Stroke / Shadow / Glow / Readability ─────────────────────────────────
Composition TextGlyph05_StrokeShadowGlowReadability() {
    return composition({
        .name = "TextGlyph05_StrokeShadowGlowReadability",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "05_stroke_shadow_glow_readability",
                      "Check: stroke follows glyph contours; shadow does not deform letterforms.");

        // Drop shadow: offset text in black behind main text
        s.text("shadow", {
            .content = "BIG TITLE",
            .style   = {kBoldFont, 80.f, Color{0, 0, 0, 0.80f}},
            .pos     = {162.f, 435.f, 0.1f}
        });

        // Simulated stroke: N, S, E, W copies in near-black
        const float off = 3.f;
        for (auto [id, dx, dy] : std::initializer_list<std::tuple<const char*, float, float>>{
            {"sk_n",  0, -off}, {"sk_s", 0, off}, {"sk_e", off, 0}, {"sk_w", -off, 0}
        }) {
            s.text(id, {
                .content = "BIG TITLE",
                .style   = {kBoldFont, 80.f, Color{0.08f, 0.08f, 0.08f, 1}},
                .pos     = {150.f + dx, 425.f + dy, 0.2f}
            });
        }

        // Main text on top
        s.text("main", {
            .content = "BIG TITLE",
            .style   = {kBoldFont, 80.f, Color{1, 1, 1, 1}},
            .pos     = {150.f, 425.f, 0.3f}
        });

        return s.build();
    });
}

// ── 06  Alpha Blending / Text Opacity ────────────────────────────────────────
Composition TextGlyph06_AlphaBlendingTextOpacity() {
    return composition({
        .name = "TextGlyph06_AlphaBlendingTextOpacity",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Checker background FIRST (so labels draw on top)
        const int cs = 40;
        for (int row = 0; row < kH / cs; ++row) {
            for (int col = 0; col < kW / cs; ++col) {
                bool light = (row + col) % 2 == 0;
                s.rect(fmt::format("ch_{}_{}", row, col), {
                    .size  = {(float)cs, (float)cs},
                    .color = light ? Color{0.20f, 0.22f, 0.30f, 1} : Color{0.12f, 0.13f, 0.19f, 1},
                    .pos   = {col * cs + cs/2.f, row * cs + cs/2.f, 0}
                });
            }
        }

        // Header labels (drawn after checker so they're on top)
        s.text("title_label", {
            .content = "06_alpha_blending_text_opacity",
            .style   = {kBoldFont, 22.0f, kTitle},
            .pos     = {40, 38, 0}
        });
        s.rect("sep", { .size = {(float)(kW - 40), 1.f}, .color = kSep,
                        .pos  = {kW/2.f + 20, 82, 0} });
        s.text("check", {
            .content = "Check: transparent text blends smoothly, no dark/white fringe.",
            .style   = {kRegFont, 18.0f, kCheck},
            .pos     = {75, 820, 0}
        });

        // Blue card
        s.rounded_rect("card", {
            .size   = {900, 700},
            .radius = 32,
            .color  = Color{0.44f, 0.48f, 0.75f, 1.0f},
            .pos    = {kW/2.f, 520, -0.1f}
        });

        const TextStyle bold{kBoldFont, 90.f, Color{1, 1, 1, 1}};

        // 100% opacity
        s.text("op100", {
            .content = "Opacity 100%",
            .style   = {kBoldFont, 90.f, Color{1, 1, 1, 1.00f}},
            .pos     = {135, 310, 0.5f}
        });

        // 65% opacity via text alpha
        s.text("op65", {
            .content = "Opacity 65%",
            .style   = {kBoldFont, 90.f, Color{1, 1, 1, 0.65f}},
            .pos     = {135, 450, 0.5f}
        });

        // 35% opacity via text alpha
        s.text("op35", {
            .content = "Opacity 35%",
            .style   = {kBoldFont, 90.f, Color{1, 1, 1, 0.35f}},
            .pos     = {135, 590, 0.5f}
        });

        return s.build();
    });
}

// ── 07  Positioning / Fractional Pixels ──────────────────────────────────────
Composition TextGlyph07_PositioningFractionalPixels() {
    return composition({
        .name = "TextGlyph07_PositioningFractionalPixels",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "07_positioning_fractional_pixels",
                      "Check: positioning is stable; fractional offsets should not jump.");

        const TextStyle st{kRegFont, 72.f, Color{1, 1, 1, 1}};
        const float base_x = 100.f;

        for (auto [id, label, xoff, y] : std::initializer_list<std::tuple<const char*, const char*, float, float>>{
            {"t0", "x=100.0  The quick brown fox", 0.0f, 190},
            {"t3", "x=100.3  The quick brown fox", 0.3f, 330},
            {"t5", "x=100.5  The quick brown fox", 0.5f, 470},
            {"t8", "x=100.8  The quick brown fox", 0.8f, 610}
        }) {
            // Red baseline indicator
            hline(s, fmt::format("bline_{}", id), y + 72.f,
                  {1.0f, 0.30f, 0.30f, 0.60f});

            s.text(id, {
                .content = label,
                .style   = st,
                .pos     = {base_x + xoff, y, 0.2f}
            });
        }

        return s.build();
    });
}

// ── 08  Mixed Scripts / Fallback ─────────────────────────────────────────────
Composition TextGlyph08_MixedScriptsFallback() {
    return composition({
        .name = "TextGlyph08_MixedScriptsFallback",
        .width = kW, .height = kH, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        draw_scaffold(s, "08_mixed_scripts_fallback",
                      "Check: fallback policy. Complex shaping/RTL may require HarfBuzz later.");

        const TextStyle st{kRegFont, 52.f, Color{1, 1, 1, 1}};
        const float x = 80.f;
        float y = 150.f;
        const float step = 110.f;

        // Latin (always works)
        s.text("latin", {
            .content = "Latin: Chronon3d text renderer",
            .style = st, .pos = {x, y, 0}
        });
        y += step;

        // Greek (Inter covers greek)
        s.text("greek", {
            .content = "Greek: \xce\x9a\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xcf\x81\xce\xb1 "
                       "\xce\xba\xcf\x8c\xcf\x83\xce\xbc\xce\xb5",  // Καλημέρα κόσμε
            .style = st, .pos = {x, y, 0}
        });
        y += step;

        // Cyrillic (Inter covers cyrillic)
        s.text("cyrillic", {
            .content = "Cyrillic: \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 "
                       "\xd0\xbc\xd0\xb8\xd1\x80",  // Привет мир
            .style = st, .pos = {x, y, 0}
        });
        y += step;

        // Arabic (RTL — Inter does not support Arabic; should fallback)
        s.text("arabic", {
            .content = "Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 "
                       "\xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85",  // مرحبا بالعالم
            .style = st, .pos = {x, y, 0}
        });
        y += step;

        // Hebrew (RTL — fallback expected)
        s.text("hebrew", {
            .content = "Hebrew: \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d "
                       "\xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d",  // שלום עולם
            .style = st, .pos = {x, y, 0}
        });
        y += step;

        // CJK (fallback expected for Inter)
        s.text("cjk", {
            .content = "CJK: \xe4\xb8\xad \xe6\x96\x87 \xe6\xbc\xa2\xe5\xad\x97",  // 中 文 漢字
            .style = st, .pos = {x, y, 0}
        });

        return s.build();
    });
}

// ── Registration ─────────────────────────────────────────────────────────────

CHRONON_REGISTER_COMPOSITION("TextGlyph01_BaselineAscendersDescenders",
                              TextGlyph01_BaselineAscendersDescenders)
CHRONON_REGISTER_COMPOSITION("TextGlyph02_KerningPairs",
                              TextGlyph02_KerningPairs)
CHRONON_REGISTER_COMPOSITION("TextGlyph03_GlyphCoverageAccentsSymbols",
                              TextGlyph03_GlyphCoverageAccentsSymbols)
CHRONON_REGISTER_COMPOSITION("TextGlyph04_MultilineWrapAlignment",
                              TextGlyph04_MultilineWrapAlignment)
CHRONON_REGISTER_COMPOSITION("TextGlyph05_StrokeShadowGlowReadability",
                              TextGlyph05_StrokeShadowGlowReadability)
CHRONON_REGISTER_COMPOSITION("TextGlyph06_AlphaBlendingTextOpacity",
                              TextGlyph06_AlphaBlendingTextOpacity)
CHRONON_REGISTER_COMPOSITION("TextGlyph07_PositioningFractionalPixels",
                              TextGlyph07_PositioningFractionalPixels)
CHRONON_REGISTER_COMPOSITION("TextGlyph08_MixedScriptsFallback",
                              TextGlyph08_MixedScriptsFallback)
