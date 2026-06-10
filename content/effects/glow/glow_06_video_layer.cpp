// content/effects/glow_06_video_layer.cpp
// TEST 6 — Video Layer: city video content wrapped with glow + title
#include "../common/glow_test_common.hpp"

namespace chronon3d::content::effects {

// ── Video source asset (rendered by CompositionFallbackVideoDecoder) ──────────
Composition glow_video_source_asset() {
    return composition({.name="GlowVideoSourceAsset",.width=404,.height=244,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const float t    = static_cast<float>(ctx.frame.frame.frame) / 120.f;
        const float wave = 0.5f + 0.5f * std::sinf(t * 6.2831853f * 2.f);

        // Sky gradient
        s.layer("sky", [](LayerBuilder& l) {
            l.rect("bg", {
                .size={404,244},.color=Color{1,1,1,1},
                .pos={-202,-122,0},
                .fill=Fill::linear({0,0},{0,1},{
                    {0.f,Color{0.11f,0.13f,0.23f,1}},
                    {0.55f,Color{0.10f,0.11f,0.18f,1}},
                    {1.f,Color{0.03f,0.04f,0.08f,1}}
                })
            });
        });

        // City buildings
        s.layer("buildings", [wave](LayerBuilder& l) {
            const float H[]={120,88,144,104,164,128,180,98,150,112};
            const float W[]={30,24,26,22,28,24,30,22,26,24};
            float x=-188.f;
            for (int i = 0; i < 10; ++i) {
                l.rect("b"+std::to_string(i), {
                    .size={W[i],H[i]},.color=Color{0.08f,0.10f,0.16f,1},
                    .pos={x,122.f-H[i],0}
                });
                for (int row=0;row<7;++row) for (int col=0;col<2;++col) {
                    if (((i+row+col+(int)(wave*4))%3)==0)
                        l.rect("w"+std::to_string(i*100+row*10+col), {
                            .size={4.f,6.f},.color=Color{0.84f,0.55f,0.84f,1},
                            .pos={x+5.f+(float)col*9.f, 122.f-H[i]+10.f+(float)row*14.f, 0}
                        });
                }
                x += W[i]+6.f;
            }
        });

        // Car with headlight glow
        s.layer("car", [wave](LayerBuilder& l) {
            const float bob = (wave-0.5f)*4.f;
            l.position({0, 42.f+bob, 0})
             .glow(12.f, 0.60f, Color{0.25f,0.70f,1,1});
            l.rect("body", {.size={58,24},.color=Color{0.06f,0.07f,0.10f,1},.pos={-29,-12,0}});
            l.rounded_rect("roof", {.size={38,18},.radius=6,.color=Color{0.05f,0.05f,0.08f,1},.pos={-19,-22,0}});
            l.rect("tl", {.size={12,4},.color=Color{1,.2f,.3f,1},.pos={-24,-8,0}});
            l.rect("tr", {.size={12,4},.color=Color{1,.2f,.3f,1},.pos={12,-8,0}});
        });

        return s.build();
    });
}

// ── Main composition ──────────────────────────────────────────────────────────
Composition glow_06_video_layer() {
    return composition({.name="GlowVideoLayer",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.01f,0.02f,0.05f,1}, Color{0.03f,0.03f,0.08f,1});

        // Large ambient city glow behind video
        s.layer("city_glow", [](LayerBuilder& l) {
            l.position({0,80,0})
             .glow(320.f,0.38f,Color{0.20f,0.40f,1.f,1.f});
            l.rect("r", {.size={1400,2},.color=Color{0,0,0,0},.pos={-700,0,0}});
        });

        // Title above with electric blue glow
        s.layer("title", [](LayerBuilder& l) {
            l.position({0,-370,0})
             .glow(22.f,1.0f,Color{0.22f,0.72f,1.f,1.f});
            l.text("t", {
                .text="VIDEO LAYER GLOW",
                .size={900,80},.pos={0,0,0},
                .font_size=46.f,
                .color=Color{0.72f,0.92f,1.f,1.f},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        // Video content with glow + drop shadow
        video::VideoSource source;
        source.path = "assets/videos/glow_test.mp4";
        source.size = {606.f, 366.f};
        s.video_layer("video_glow", source, [](LayerBuilder& l) {
            l.position({0,-20,0})
             .glow(32.f, 1.0f, Color{0.28f,0.72f,1.f,1.f})
             .drop_shadow({0,14}, Color{0,0,0,0.65f}, 20.f);
        });

        bottom_label(s, "TEST 6 — Video Layer: city animation wrapped with 32px glow (source_alpha verified)");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowVideoSourceAsset", chronon3d::content::effects::glow_video_source_asset)
CHRONON_REGISTER_COMPOSITION("GlowVideoLayer",       chronon3d::content::effects::glow_06_video_layer)
