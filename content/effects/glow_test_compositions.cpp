// content/effects/glow_test_compositions.cpp

#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chronon3d::content::effects {

static void dark_background(SceneBuilder& s) {
    s.layer("background", [](LayerBuilder& l) {
        l.position({0, 0, 0});
        l.rect("bg", {
            .size = {1920, 1080},
            .color = Color{0.015f, 0.018f, 0.030f, 1.0f},
            .pos = {-960, -540, 0}
        });
    });
}

static void add_header(SceneBuilder& s, const std::string& num_title, const std::string& sub) {
    s.layer(num_title + "_header", [=](LayerBuilder& l) {
        l.position({-880, -450, 0});
        l.text("title", {
            .text = num_title,
            .size = {800, 50},
            .pos = {0, 0, 0},
            .font_size = 32.0f,
            .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
            .align = TextAlign::Left
        });
        l.text("subtitle", {
            .text = sub,
            .size = {800, 40},
            .pos = {0, 50, 0},
            .font_size = 20.0f,
            .color = Color{0.5f, 0.55f, 0.65f, 1.0f},
            .align = TextAlign::Left
        });
    });
}

static void add_description(SceneBuilder& s, const std::string& line1, const std::string& line2) {
    s.layer(line1 + "_desc", [=](LayerBuilder& l) {
        l.position({0, 410, 0});
        l.text("desc1", {
            .text = line1,
            .size = {1400, 40},
            .pos = {-700, -20, 0},
            .font_size = 24.0f,
            .color = Color{0.5f, 0.55f, 0.65f, 1.0f},
            .align = TextAlign::Center
        });
        l.text("desc2", {
            .text = line2,
            .size = {1400, 40},
            .pos = {-700, 20, 0},
            .font_size = 24.0f,
            .color = Color{0.5f, 0.55f, 0.65f, 1.0f},
            .align = TextAlign::Center
        });
    });
}

// TEST 1
// Serve per vedere se il glow esce davvero fuori dai bordi dell'oggetto.
Composition glow_rect_basic() {
    return composition({.name = "GlowRectBasic", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_background(s);
        add_header(s, "1. GlowRectBasic", "Glow su rettangolo arrotondato");
        add_description(s, "Il glow esce dai bordi in modo morbido e uniforme.", "Il centro resta nitido e scuro.");

        s.layer("card_glow", [&](LayerBuilder& l) {
            l.position({0, -10, 0})
             .glow(38.0f, 0.95f, Color{0.1f, 0.65f, 1.0f, 1.0f});

            l.rounded_rect("card", {
                .size = {620, 320},
                .radius = 42.0f,
                .color = Color{0.08f, 0.12f, 0.22f, 1.0f},
                .pos = {-310, -160, 0}
            });
        });

        s.layer("title", [](LayerBuilder& l) {
            l.position({0, -10, 0});
            l.text("label", {
                .text = "BASIC GLOW",
                .size = {700, 100},
                .pos = {-350, -50, 0},
                .font_size = 72.0f,
                .color = Color{1, 1, 1, 1},
                .align = TextAlign::Center
            });
        });

        return s.build();
    });
}

// TEST 2
// Serve per confrontare radius e intensity nello stesso frame.
Composition glow_radius_intensity_ladder() {
    return composition({.name = "GlowRadiusIntensityLadder", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_background(s);
        add_header(s, "2. GlowRadiusIntensityLadder", "Confronto radius e intensity");
        add_description(s, "Si vede chiaramente la differenza di raggio e intensità.", "Il glow rimane morbido e pulito.");

        const float xs[4] = {-540, -180, 180, 540};
        const float radius[4] = {8, 20, 42, 80};
        const float intensity[4] = {0.35f, 0.65f, 0.9f, 1.2f};

        for (int i = 0; i < 4; ++i) {
            s.layer("orb_" + std::to_string(i), [&](LayerBuilder& l) {
                l.position({xs[i], -50, 0})
                 .glow(radius[i], intensity[i], Color{0.2f, 0.8f, 1.0f, 1.0f});

                l.circle("circle", {
                    .radius = 74.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0, 0, 0}
                });
            });

            char buf[64];
            snprintf(buf, sizeof(buf), "R %d\nI %.2f", (int)radius[i], intensity[i]);
            s.layer("label_" + std::to_string(i), [&](LayerBuilder& l) {
                l.position({xs[i], 160, 0});
                l.text("label", {
                    .text = buf,
                    .size = {240, 80},
                    .pos = {-120, -40, 0},
                    .font_size = 28.0f,
                    .color = Color{0.5f, 0.55f, 0.65f, 1.0f},
                    .align = TextAlign::Center,
                    .line_height = 1.3f
                });
            });
        }

        return s.build();
    });
}

// TEST 3
// Serve per vedere se il glow funziona bene sulle frasi/testo.
Composition glow_phrase_text() {
    return composition({.name = "GlowPhraseText", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_background(s);
        add_header(s, "3. GlowPhraseText", "Glow su frasi / testo");
        add_description(s, "Il testo rimane nitido e leggibile.", "Il glow crea un'aura luminosa dietro le lettere.");

        s.layer("phrase_glow", [&](LayerBuilder& l) {
            l.position({0, -80, 0})
             .glow(26.0f, 1.0f, Color{1.0f, 0.28f, 0.08f, 1.0f});

            l.text("main_phrase", {
                .text = "THIS CHANGES EVERYTHING",
                .size = {1500, 140},
                .pos = {-750, -70, 0},
                .font_size = 88.0f,
                .color = Color{1, 1, 1, 1},
                .align = TextAlign::Center
            });
        });

        s.layer("subtitle", [&](LayerBuilder& l) {
            l.position({0, 120, 0})
             .glow(14.0f, 0.55f, Color{0.4f, 0.75f, 1.0f, 1.0f});

            l.text("sub", {
                .text = "Glow must keep text sharp, not blurry",
                .size = {1200, 70},
                .pos = {-600, -35, 0},
                .font_size = 44.0f,
                .color = Color{0.4f, 0.75f, 1.0f, 1.0f},
                .align = TextAlign::Center
            });
        });

        return s.build();
    });
}

// TEST 4
// Serve per vedere se il glow funziona sulle immagini.
Composition glow_image_card() {
    return composition({.name = "GlowImageCard", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_background(s);
        add_header(s, "4. GlowImageCard", "Glow su immagine");
        add_description(s, "Il glow segue i bordi dell'immagine.", "I dettagli interni restano nitidi e naturali.");

        s.layer("image_layer", [&](LayerBuilder& l) {
            l.position({0, -60, 0})
             .glow(34.0f, 0.85f, Color{0.95f, 0.75f, 0.25f, 1.0f})
             .drop_shadow({0, 18}, Color{0, 0, 0, 0.55f}, 24.0f);

            l.image("image", {
                .path = "assets/images/camera_reference.jpg",
                .size = {720, 420},
                .pos = {-360, -210, 0},
                .opacity = 1.0f
            });
        });

        s.layer("label", [&](LayerBuilder& l) {
            l.position({0, 240, 0});
            l.text("label", {
                .text = "IMAGE + GLOW + SHADOW",
                .size = {900, 80},
                .pos = {-450, -40, 0},
                .font_size = 48.0f,
                .color = Color{0.95f, 0.75f, 0.25f, 1.0f},
                .align = TextAlign::Center
            });
        });

        return s.build();
    });
}

// TEST 5
// Serve per vedere se il glow animato pulsa senza flicker.
Composition glow_pulse_animation() {
    return composition({.name = "GlowPulseAnimation", .width = 1920, .height = 1080, .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_background(s);
        add_header(s, "5. GlowPulseAnimation", "Glow animato (pulsazione)");
        add_description(s, "Il glow pulsa in modo fluido e senza flicker.", "L'animazione è morbida e piacevole.");

        const float t = static_cast<float>(ctx.frame) / 120.0f;
        const float pulse = 0.5f + 0.5f * std::sin(t * 6.2831853f * 2.0f);
        const float radius = 16.0f + pulse * 58.0f;
        const float intensity = 0.45f + pulse * 0.75f;

        s.layer("pulse_orb", [&](LayerBuilder& l) {
            l.position({0, -50, 0})
             .glow(radius, intensity, Color{0.35f, 0.95f, 1.0f, 1.0f});

            l.circle("orb", {
                .radius = 115.0f,
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("text", [&](LayerBuilder& l) {
            l.position({0, 210, 0})
             .glow(18.0f, 0.65f, Color{0.35f, 0.95f, 1.0f, 1.0f});

            l.text("label", {
                .text = "PULSING GLOW TEST",
                .size = {1000, 90},
                .pos = {-500, -45, 0},
                .font_size = 58.0f,
                .color = Color{0.35f, 0.95f, 1.0f, 1.0f},
                .align = TextAlign::Center
            });
        });

        return s.build();
    });
}

// TEST 6
// Serve per vedere se il glow funziona su video.
Composition glow_video_layer() {
    return composition({.name = "GlowVideoLayer", .width = 1920, .height = 1080, .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_background(s);
        add_header(s, "6. GlowVideoLayer", "Glow su video layer");
        add_description(s, "Il glow avvolge il contenuto del video.", "I movimenti rimangono nitidi e stabili.");

        s.video_layer("video_glow", "assets/videos/glow_test.mp4", [&](LayerBuilder& l) {
            l.position({0, -60, 0})
             .video_size({720, 405})
             .anchor({360, 202.5f, 0})
             .glow(36.0f, 0.75f, Color{0.4f, 0.7f, 1.0f, 1.0f})
             .drop_shadow({0, 20}, Color{0, 0, 0, 0.55f}, 28.0f);
        });

        s.layer("label", [&](LayerBuilder& l) {
            l.position({0, 240, 0});
            l.text("label", {
                .text = "VIDEO LAYER GLOW",
                .size = {900, 80},
                .pos = {-450, -40, 0},
                .font_size = 54.0f,
                .color = Color{0.4f, 0.7f, 1.0f, 1.0f},
                .align = TextAlign::Center
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowRectBasic", chronon3d::content::effects::glow_rect_basic)
CHRONON_REGISTER_COMPOSITION("GlowRadiusIntensityLadder", chronon3d::content::effects::glow_radius_intensity_ladder)
CHRONON_REGISTER_COMPOSITION("GlowPhraseText", chronon3d::content::effects::glow_phrase_text)
CHRONON_REGISTER_COMPOSITION("GlowImageCard", chronon3d::content::effects::glow_image_card)
CHRONON_REGISTER_COMPOSITION("GlowPulseAnimation", chronon3d::content::effects::glow_pulse_animation)
CHRONON_REGISTER_COMPOSITION("GlowVideoLayer", chronon3d::content::effects::glow_video_layer)
