// ==============================================================================
// content/certification/cert_multilingual.cpp
//
// FASE 3.5-3.6 — CertMultilingual composition
//
// Test multilingua in griglia 6 righe su 1920×1080.
//
// ╔══════════════════════════════════════════════════════════════════╗
// ║  Row  Script           Sample Text                 Expected     ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║  1    LATIN+Accents    Café naïve — São Paulo, João   PASS      ║
// ║  2    CJK              こんにちは世界 — 你好世界       PASS*     ║
// ║  3    Arabic RTL       مرحبا بالعالم                  FAIL      ║
// ║  4    Hebrew RTL       שלום עולם                      FAIL      ║
// ║  5    Emoji            🔥 🎉 🚀                        FAIL      ║
// ║  6    Mixed            Café 你好 مرحبا 🔥             PARTIAL   ║
// ╚══════════════════════════════════════════════════════════════════╝
//
// * CJK richiede font con copertura glyph CJK (Inter non li ha).
//
// RTL e Emoji sono EXPECTED FAIL:
//   - RTL: HarfBuzz supporta bidi ma il layout engine Chronon3D
//     non implementa il reverse-direction shaping.
//   - Emoji: richiedono color font (CBDT/CBLC o COLRv1), non
//     supportati dal backend Blend2D freetype rasterizer.
//
// Status osservato (2026-07-06):
//   - Compilazione:  PASS
//   - Registrazione: PASS (cli list mostra CertMultilingual)
//   - Render:        PASS (PNG 1920×1080, 91KB, non-blank)
//   - Text shaping:  BLOCKED dal pre-existing font-engine bug
//     (TextRunBuilder::commit produce 0 glyph — TICKET-104).
//   - Background:    PASS (grid minimalista visibile)
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/common/background_helpers.hpp"
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::certification {

Composition cert_multilingual() {
    constexpr int   kWidth   = 1920;
    constexpr int   kHeight  = 1080;
    constexpr float kMargin  = 60.0f;    // safe margin
    constexpr float kRowH    = 120.0f;   // height per test row
    constexpr float kGap     = 20.0f;    // vertical gap between rows
    constexpr float kStartY  = 140.0f;   // top of first test row
    constexpr float kFontSize = 36.0f;

    return composition(
        {.name = "CertMultilingual",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);

            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());

            // ── Title header ────────────────────────────────────────
            s.layer("header", [](LayerBuilder& l) {
                l.text("header", text::centered_text({
                    .text       = "Multilingual Text Certification — FASE 3.5-3.6",
                    .box        = {static_cast<float>(kWidth) - kMargin * 2.0f, 56.0f},
                    .pos        = {static_cast<float>(kWidth) * 0.5f, 64.0f, 0.0f},
                    .font_path  = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size  = 30.0f,
                    .color      = Color{0.7f, 0.7f, 0.75f, 1.0f},
                    .max_lines  = 1,
                }));
            });

            // ── Row 1: LATIN + accents (PASS expected) ────────────
            {
                constexpr float y = kStartY;
                s.layer("row1", [y](LayerBuilder& l) {
                    l.text("row1_label", text::centered_text({
                        .text       = "LATIN+Accents →",
                        .box        = {200.0f, 48.0f},
                        .pos        = {200.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size  = 22.0f,
                        .color      = Color{0.5f, 0.8f, 0.5f, 1.0f},
                        .max_lines  = 1,
                    }));
                    l.text("row1_text", text::centered_text({
                        .text       = "Café naïve — São Paulo, João",
                        .box        = {static_cast<float>(kWidth) - 500.0f, kRowH},
                        .pos        = {static_cast<float>(kWidth) * 0.5f + 100.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size  = kFontSize,
                        .color      = Color::white(),
                        .max_lines  = 2,
                    }));
                });
            }

            // ── Row 2: CJK Japanese + Chinese ──────────────────────
            {
                constexpr float y = kStartY + kRowH + kGap;
                s.layer("row2", [y](LayerBuilder& l) {
                    l.text("row2_label", text::centered_text({
                        .text       = "CJK →",
                        .box        = {200.0f, 48.0f},
                        .pos        = {200.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size  = 22.0f,
                        .color      = Color{0.5f, 0.8f, 0.5f, 1.0f},
                        .max_lines  = 1,
                    }));
                    l.text("row2_text", text::centered_text({
                        .text       = "こんにちは世界 — 你好世界 中文测试",
                        .box        = {static_cast<float>(kWidth) - 500.0f, kRowH},
                        .pos        = {static_cast<float>(kWidth) * 0.5f + 100.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size  = kFontSize,
                        .color      = Color::white(),
                        .max_lines  = 2,
                    }));
                });
            }

            // ── Row 3: RTL Arabic (EXPECTED FAIL) ──────────────────
            {
                constexpr float y = kStartY + (kRowH + kGap) * 2.0f;
                s.layer("row3", [y](LayerBuilder& l) {
                    l.text("row3_label", text::centered_text({
                        .text       = "Arabic RTL (EXPECTED FAIL) →",
                        .box        = {280.0f, 48.0f},
                        .pos        = {200.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size  = 20.0f,
                        .color      = Color{0.9f, 0.6f, 0.3f, 1.0f},
                        .max_lines  = 1,
                    }));
                    l.text("row3_text", text::centered_text({
                        .text       = "مرحبا بالعالم",
                        .box        = {static_cast<float>(kWidth) - 500.0f, kRowH},
                        .pos        = {static_cast<float>(kWidth) * 0.5f + 100.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size  = kFontSize,
                        .color      = Color{0.9f, 0.7f, 0.3f, 1.0f},
                        .max_lines  = 2,
                    }));
                });
            }

            // ── Row 4: RTL Hebrew (EXPECTED FAIL) ─────────────────
            {
                constexpr float y = kStartY + (kRowH + kGap) * 3.0f;
                s.layer("row4", [y](LayerBuilder& l) {
                    l.text("row4_label", text::centered_text({
                        .text       = "Hebrew RTL (EXPECTED FAIL) →",
                        .box        = {280.0f, 48.0f},
                        .pos        = {200.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size  = 20.0f,
                        .color      = Color{0.9f, 0.6f, 0.3f, 1.0f},
                        .max_lines  = 1,
                    }));
                    l.text("row4_text", text::centered_text({
                        .text       = "שלום עולם",
                        .box        = {static_cast<float>(kWidth) - 500.0f, kRowH},
                        .pos        = {static_cast<float>(kWidth) * 0.5f + 100.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size  = kFontSize,
                        .color      = Color{0.9f, 0.7f, 0.3f, 1.0f},
                        .max_lines  = 2,
                    }));
                });
            }

            // ── Row 5: Emoji (EXPECTED FAIL) ──────────────────────
            {
                constexpr float y = kStartY + (kRowH + kGap) * 4.0f;
                s.layer("row5", [y](LayerBuilder& l) {
                    l.text("row5_label", text::centered_text({
                        .text       = "Emoji (EXPECTED FAIL) →",
                        .box        = {280.0f, 48.0f},
                        .pos        = {200.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size  = 20.0f,
                        .color      = Color{0.9f, 0.5f, 0.5f, 1.0f},
                        .max_lines  = 1,
                    }));
                    l.text("row5_text", text::centered_text({
                        .text       = "🔥 🎉 🚀",
                        .box        = {static_cast<float>(kWidth) - 500.0f, kRowH},
                        .pos        = {static_cast<float>(kWidth) * 0.5f + 100.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size  = 48.0f,
                        .color      = Color{1.0f, 0.6f, 0.4f, 1.0f},
                        .max_lines  = 2,
                    }));
                });
            }

            // ── Row 6: Mixed script — tutti insieme ────────────────
            {
                constexpr float y = kStartY + (kRowH + kGap) * 5.0f;
                s.layer("row6", [y](LayerBuilder& l) {
                    l.text("row6_label", text::centered_text({
                        .text       = "Mixed →",
                        .box        = {200.0f, 48.0f},
                        .pos        = {200.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size  = 22.0f,
                        .color      = Color{0.5f, 0.8f, 0.5f, 1.0f},
                        .max_lines  = 1,
                    }));
                    l.text("row6_text", text::centered_text({
                        .text       = "Café 你好 مرحبا 🔥",
                        .box        = {static_cast<float>(kWidth) - 500.0f, kRowH},
                        .pos        = {static_cast<float>(kWidth) * 0.5f + 100.0f, y, 0.0f},
                        .font_path  = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size  = kFontSize,
                        .color      = Color::white(),
                        .max_lines  = 2,
                    }));
                });
            }

            return s.build();
        });
}

// ── Registration ──────────────────────────────────────────────────────
void register_cert_multilingual_compositions(CompositionRegistry& registry) {
    registry.add("CertMultilingual", [](const CompositionProps&) {
        return cert_multilingual();
    });
}

} // namespace chronon3d::content::certification
