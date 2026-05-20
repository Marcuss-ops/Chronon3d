// PhrasePresetGallery
//   chronon3d_cli render PhrasePresetGallery --graph --frames 0   -o output/proofs/phrase_preset_gallery_f000.png
//   chronon3d_cli render PhrasePresetGallery --graph --frames 45  -o output/proofs/phrase_preset_gallery_f045.png
//   chronon3d_cli render PhrasePresetGallery --graph --frames 90  -o output/proofs/phrase_preset_gallery_f090.png

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

#include <array>

using namespace chronon3d;
using namespace chronon3d::presets::phrase;

static Composition phrase_preset_gallery() {
    return composition({
        .name = "PhrasePresetGallery",
        .width = 1920,
        .height = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.04f, 0.04f, 0.05f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.04f},
            .spacing = 64.0f,
            .extent = 3200.0f,
            .centered = true,
        });

        const std::array<PhrasePreset, 5> presets{
            PhrasePreset::TitlePop,
            PhrasePreset::LowerThird,
            PhrasePreset::CaptionClean,
            PhrasePreset::QuoteImpact,
            PhrasePreset::WarningPulse,
        };

        const std::array<Vec3, 5> positions{
            Vec3{-620.0f, -220.0f, 0.0f},
            Vec3{580.0f, -220.0f, 0.0f},
            Vec3{-620.0f, 220.0f, 0.0f},
            Vec3{580.0f, 220.0f, 0.0f},
            Vec3{0.0f, 0.0f, 0.0f},
        };

        const std::array<PhraseParams, 5> phrases{{
            PhraseParams{
                .id = "title_pop",
                .text = "THIS CHANGES EVERYTHING",
                .subtitle = "A headline that lands hard",
                .start = 0,
                .end = 90,
                .position = positions[0],
                .box_size = {760.0f, 220.0f},
                .title_size = {650.0f, 120.0f},
                .subtitle_size = {620.0f, 44.0f},
                .title_font_size = 60.0f,
                .subtitle_font_size = 24.0f,
                .panel_color = Color{0.05f, 0.06f, 0.08f, 0.88f},
                .accent_color = Color{0.97f, 0.58f, 0.18f, 1.0f},
            },
            PhraseParams{
                .id = "lower_third",
                .text = "MARCUS HILL",
                .subtitle = "Creative Director",
                .start = 0,
                .end = 90,
                .position = positions[1],
                .box_size = {820.0f, 180.0f},
                .title_size = {560.0f, 100.0f},
                .subtitle_size = {560.0f, 40.0f},
                .title_font_size = 46.0f,
                .subtitle_font_size = 22.0f,
                .panel_color = Color{0.06f, 0.07f, 0.09f, 0.84f},
                .accent_color = Color{0.46f, 0.88f, 0.65f, 1.0f},
            },
            PhraseParams{
                .id = "caption_clean",
                .text = "A clean caption style for narration",
                .subtitle = "minimal, readable, and calm",
                .start = 0,
                .end = 90,
                .position = positions[2],
                .box_size = {860.0f, 200.0f},
                .title_size = {760.0f, 100.0f},
                .subtitle_size = {620.0f, 40.0f},
                .title_font_size = 44.0f,
                .subtitle_font_size = 20.0f,
                .panel_color = Color{0.0f, 0.0f, 0.0f, 0.0f},
                .accent_color = Color{0.72f, 0.80f, 0.95f, 1.0f},
                .text_color = Color{0.95f, 0.97f, 0.99f, 1.0f},
            },
            PhraseParams{
                .id = "quote_impact",
                .text = "Simplicity makes the message hit harder.",
                .subtitle = "Design note",
                .start = 0,
                .end = 90,
                .position = positions[3],
                .box_size = {860.0f, 230.0f},
                .title_size = {760.0f, 110.0f},
                .subtitle_size = {500.0f, 40.0f},
                .title_font_size = 42.0f,
                .subtitle_font_size = 20.0f,
                .panel_color = Color{0.05f, 0.05f, 0.07f, 0.84f},
                .accent_color = Color{0.96f, 0.71f, 0.44f, 1.0f},
            },
            PhraseParams{
                .id = "warning_pulse",
                .text = "BREAKING",
                .subtitle = "Motion preset: warning pulse",
                .start = 0,
                .end = 90,
                .position = positions[4],
                .box_size = {720.0f, 210.0f},
                .title_size = {620.0f, 120.0f},
                .subtitle_size = {560.0f, 40.0f},
                .title_font_size = 58.0f,
                .subtitle_font_size = 22.0f,
                .panel_color = Color{0.16f, 0.04f, 0.05f, 0.88f},
                .accent_color = Color{1.0f, 0.38f, 0.28f, 1.0f},
                .text_color = Color{1.0f, 0.95f, 0.94f, 1.0f},
            },
        }};

        for (usize i = 0; i < presets.size(); ++i) {
            draw_phrase(s, ctx, presets[i], phrases[i]);
        }

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("PhrasePresetGallery", phrase_preset_gallery)
