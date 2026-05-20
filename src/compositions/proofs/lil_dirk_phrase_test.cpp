// LilDirkPhraseTest
//   chronon3d_cli video LilDirkPhraseTest --graph --start 0 --end 120 --fps 30 -o output/proofs/lil_dirk_phrase_test.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/compositions/proofs/lil_dirk_clean.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/presets/phrase/phrase_presets.hpp>

using namespace chronon3d;
using namespace chronon3d::compositions;
using namespace chronon3d::presets::phrase;

static Composition lil_dirk_phrase_test() {
    return composition({
        .name = "LilDirkPhraseTest",
        .width = 1920,
        .height = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        build_lil_dirk_clean_background(s, ctx);

        draw_phrase(s, ctx, PhrasePreset::TypewriterCaption, PhraseParams{
            .id = "intro",
            .text = "THIS CHANGES EVERYTHING",
            .subtitle = "Typewriter caption over Lil Dirk background",
            .start = 0,
            .end = 35,
            .position = {0.0f, -310.0f, 0.0f},
            .box_size = {1020.0f, 240.0f},
            .title_size = {860.0f, 120.0f},
            .subtitle_size = {780.0f, 44.0f},
            .title_font_size = 64.0f,
            .subtitle_font_size = 24.0f,
            .panel_color = Color{0.04f, 0.05f, 0.07f, 0.86f},
            .accent_color = Color{0.98f, 0.54f, 0.16f, 1.0f},
            .text_color = Color{0.99f, 0.99f, 1.0f, 1.0f},
        });

        draw_phrase(s, ctx, PhrasePreset::BounceTitle, PhraseParams{
            .id = "middle",
            .text = "KINETIC BOUNCE",
            .subtitle = "The same background, different phrase motion",
            .start = 35,
            .end = 75,
            .position = {0.0f, 40.0f, 0.0f},
            .box_size = {980.0f, 230.0f},
            .title_size = {840.0f, 120.0f},
            .subtitle_size = {760.0f, 44.0f},
            .title_font_size = 62.0f,
            .subtitle_font_size = 22.0f,
            .panel_color = Color{0.06f, 0.06f, 0.09f, 0.84f},
            .accent_color = Color{0.55f, 0.95f, 0.74f, 1.0f},
            .text_color = Color{1.0f, 1.0f, 1.0f, 1.0f},
        });

        draw_phrase(s, ctx, PhrasePreset::GlitchBanner, PhraseParams{
            .id = "outro",
            .text = "GLITCH BANNER",
            .subtitle = "Short burst of instability",
            .start = 75,
            .end = 120,
            .position = {0.0f, 310.0f, 0.0f},
            .box_size = {980.0f, 220.0f},
            .title_size = {820.0f, 120.0f},
            .subtitle_size = {680.0f, 44.0f},
            .title_font_size = 60.0f,
            .subtitle_font_size = 22.0f,
            .panel_color = Color{0.10f, 0.03f, 0.06f, 0.86f},
            .accent_color = Color{1.0f, 0.37f, 0.72f, 1.0f},
            .text_color = Color{1.0f, 0.97f, 0.99f, 1.0f},
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkPhraseTest", lil_dirk_phrase_test)
