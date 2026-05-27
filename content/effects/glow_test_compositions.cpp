// content/effects/glow_test_compositions.cpp
// Six cinematic glow test compositions

#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <cmath>
#include <string>

namespace chronon3d::content::effects {

static constexpr i32 W = 1920;
static constexpr i32 H = 1080;
static constexpr f32 HW = W * 0.5f;
static constexpr f32 HH = H * 0.5f;

static void deep_bg(SceneBuilder& s, Color top, Color bot) {
    s.layer("_bg", [=](LayerBuilder& l) {
        l.rect("bg", {
            .size = {(f32)W, (f32)H},
            .color = top,
            .pos = {-HW, -HH, 0.f},
            .fill = Fill::linear({0,0},{0,1},{
                {0.f, top},
                {1.f, bot}
            })
        });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1 — Neon Sign: big glowing text, multiple colored halos
// ─────────────────────────────────────────────────────────────────────────────
Composition glow_neon_sign() {
    return composition({.name="GlowNeonSign",.width=W,.height=H,.duration=120},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.01f,0.01f,0.03f,1}, Color{0.04f,0.02f,0.06f,1});

        // Subtle horizontal grid lines
        s.layer("grid", [](LayerBuilder& l){
            for (int i = 0; i < 12; ++i) {
                float y = -HH + 90.f*i;
                l.line("h"+std::to_string(i), {
                    .from={-HW,y,0},{.to={HW,y,0},
                    .thickness=1.f,
                    .color=Color{1,1,1,0.04f}
                });
            }
        });

        // Main hero text — big electric blue glow
        s.layer("hero", [](LayerBuilder& l){
            l.position({0,-120,0})
             .glow(48.f, 1.6f, Color{0.18f,0.62f,1.f,1.f});
            l.text("t", {
                .text="NEON\nGLOW",
                .size={900,320},
                .pos={0,0,0},
                .font_size=140.f,
                .color=Color{0.88f,0.96f,1.f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle,
                .line_height=0.90f
            });
        });

        // Subtitle — magenta glow
        s.layer("sub", [](LayerBuilder& l){
            l.position({0, 180, 0})
             .glow(18.f, 1.1f, Color{1.f,0.22f,0.72f,1.f});
            l.text("t", {
                .text="CHRONON3D GLOW PIPELINE",
                .size={900,60},
                .pos={0,0,0},
                .font_size=28.f,
                .color=Color{1.f,0.60f,0.90f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle
            });
        });

        // Horizontal neon bar top
        s.layer("bar_top", [](LayerBuilder& l){
            l.position({0,-300,0})
             .glow(12.f, 1.3f, Color{0.18f,0.62f,1.f,1.f});
            l.rect("r", {.size={700,3},.color=Color{0.4f,0.8f,1.f,0.9f},.pos={-350,-1.5f,0}});
        });

        // Horizontal neon bar bottom
        s.layer("bar_bot", [](LayerBuilder& l){
            l.position({0, 270, 0})
             .glow(12.f, 1.3f, Color{1.f,0.22f,0.72f,1.f});
            l.rect("r", {.size={700,3},.color=Color{1.f,0.5f,0.9f,0.9f},.pos={-350,-1.5f,0}});
        });

        return s.build();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2 — Orb Galaxy: 6 colored orbs with different glow radii
// ─────────────────────────────────────────────────────────────────────────────
Composition glow_orb_galaxy() {
    return composition({.name="GlowOrbGalaxy",.width=W,.height=H,.duration=120},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.02f,0.01f,0.05f,1}, Color{0.01f,0.01f,0.02f,1});

        // Subtle star field
        s.layer("stars", [](LayerBuilder& l){
            const float sx[20]={-880,-720,-560,-400,-240,-80,80,240,400,560,720,880,
                                -640,-320,0,320,640,-480,160,480};
            const float sy[20]={-380,-260,-400,-300,-420,-350,-380,-280,-420,-340,
                                -260,-390,-150,-200,-180,-160,-140,-440,-460,-480};
            for(int i=0;i<20;++i){
                l.circle("s"+std::to_string(i),{
                    .radius=1.8f+0.6f*(i%3),
                    .color=Color{1,1,1,0.35f+0.1f*(i%4)},
                    .pos={sx[i],sy[i],0}
                });
            }
        });

        struct OrbDef { float x,y,r,rad,inten; Color col; };
        OrbDef orbs[6]={
            {-560,-100, 36, 55, 1.4f, Color{0.20f,0.60f,1.0f,1}},
            {-200, 80,  28, 38, 1.1f, Color{0.90f,0.28f,1.0f,1}},
            { 0, -180,  44, 70, 1.7f, Color{0.22f,0.95f,0.72f,1}},
            { 220, 100, 22, 30, 0.9f, Color{1.0f, 0.56f,0.18f,1}},
            { 480,-60,  32, 50, 1.3f, Color{1.0f, 0.24f,0.52f,1}},
            {-380, 200, 18, 24, 0.8f, Color{0.60f,0.88f,1.0f,1}},
        };

        for(int i=0;i<6;++i){
            const int idx=i;
            s.layer("orb"+std::to_string(idx),[orbs,idx](LayerBuilder& l){
                const auto& o=orbs[idx];
                l.position({o.x,o.y,0})
                 .glow(o.rad,o.inten,o.col);
                l.circle("c",{.radius=o.r,.color=Color{1,1,1,0.95f},.pos={0,0,0}});
            });
        }

        // Central label
        s.layer("label",[](LayerBuilder& l){
            l.position({0,360,0});
            l.text("t",{
                .text="GLOW RADIUS & INTENSITY LADDER",
                .size={900,50},.pos={0,0,0},
                .font_size=22.f,.color=Color{0.7f,0.75f,0.85f,0.9f},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3 — Cinematic Text: layered phrase with golden + cyan glow
// ─────────────────────────────────────────────────────────────────────────────
Composition glow_cinematic_text() {
    return composition({.name="GlowCinematicText",.width=W,.height=H,.duration=120},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.02f,0.02f,0.04f,1}, Color{0.06f,0.04f,0.02f,1});

        // Decorative rule top
        s.layer("rule_top",[](LayerBuilder& l){
            l.position({0,-280,0});
            l.rect("r",{.size={600,1},.color=Color{0.9f,0.75f,0.3f,0.5f},.pos={-300,0,0}});
        });

        // Hero text — warm gold glow
        s.layer("hero",[](LayerBuilder& l){
            l.position({0,-80,0})
             .glow(40.f,1.4f,Color{1.f,0.78f,0.18f,1.f});
            l.text("t",{
                .text="THIS CHANGES\nEVERYTHING",
                .size={1100,340},.pos={0,0,0},
                .font_size=120.f,
                .color=Color{1.f,0.94f,0.80f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle,
                .line_height=0.92f
            });
        });

        // Tagline — electric cyan glow
        s.layer("tagline",[](LayerBuilder& l){
            l.position({0,220,0})
             .glow(20.f,1.0f,Color{0.22f,0.88f,1.f,1.f});
            l.text("t",{
                .text="Glow must keep text sharp and luminous",
                .size={900,50},.pos={0,0,0},
                .font_size=24.f,
                .color=Color{0.60f,0.92f,1.f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle
            });
        });

        // Decorative rule bottom
        s.layer("rule_bot",[](LayerBuilder& l){
            l.position({0,290,0});
            l.rect("r",{.size={600,1},.color=Color{0.9f,0.75f,0.3f,0.5f},.pos={-300,0,0}});
        });

        return s.build();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4 — UI Card Showcase: glassmorphism-style card with glow border
// ─────────────────────────────────────────────────────────────────────────────
Composition glow_ui_card() {
    return composition({.name="GlowUICard",.width=W,.height=H,.duration=90},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.04f,0.04f,0.10f,1}, Color{0.02f,0.02f,0.06f,1});

        // Background ambient orbs
        s.layer("amb1",[](LayerBuilder& l){
            l.position({-400,-200,0})
             .glow(180.f,0.55f,Color{0.20f,0.40f,1.f,1.f});
            l.circle("c",{.radius=80.f,.color=Color{0.1f,0.2f,0.6f,0.3f},.pos={0,0,0}});
        });
        s.layer("amb2",[](LayerBuilder& l){
            l.position({400,200,0})
             .glow(140.f,0.45f,Color{0.80f,0.20f,1.f,1.f});
            l.circle("c",{.radius=60.f,.color=Color{0.4f,0.1f,0.5f,0.3f},.pos={0,0,0}});
        });

        // Card background (dark glass)
        s.layer("card_bg",[](LayerBuilder& l){
            l.rounded_rect("bg",{
                .size={700,420},.radius=24.f,
                .color=Color{0.08f,0.09f,0.14f,0.95f},
                .pos={-350,-210,0}
            });
        });

        // Card glow border
        s.layer("card_border",[](LayerBuilder& l){
            l.position({0,0,0})
             .glow(22.f,1.0f,Color{0.22f,0.60f,1.f,1.f});
            // Thin rounded outline using path
            const float hw=350.f,hh=210.f,r=24.f,k=r*0.552f;
            l.path("border",{
                .commands={
                    PathCommand::move_to({-hw+r,-hh}),
                    PathCommand::line_to({hw-r,-hh}),
                    PathCommand::cubic_to({hw-r+k,-hh},{hw,-hh+r-k},{hw,-hh+r}),
                    PathCommand::line_to({hw,hh-r}),
                    PathCommand::cubic_to({hw,hh-r+k},{hw-r+k,hh},{hw-r,hh}),
                    PathCommand::line_to({-hw+r,hh}),
                    PathCommand::cubic_to({-hw+r-k,hh},{-hw,hh-r+k},{-hw,hh-r}),
                    PathCommand::line_to({-hw,-hh+r}),
                    PathCommand::cubic_to({-hw,-hh+r-k},{-hw+r-k,-hh},{-hw+r,-hh}),
                    PathCommand::close(),
                },
                .stroke={.width=1.5f,.cap=LineCap::Round,.join=LineJoin::Round},
                .fill=Fill::solid_color(Color{0,0,0,0}),
                .color=Color{0.30f,0.60f,1.f,0.70f},
                .closed=true
            });
        });

        // Card icon — glowing circle
        s.layer("icon",[](LayerBuilder& l){
            l.position({0,-60,0})
             .glow(30.f,1.2f,Color{0.22f,0.80f,1.f,1.f});
            l.circle("c",{.radius=38.f,.color=Color{0.22f,0.70f,1.f,0.90f},.pos={0,0,0}});
        });

        // Card title
        s.layer("card_title",[](LayerBuilder& l){
            l.position({0,80,0});
            l.text("t",{
                .text="GLOW UI CARD",
                .size={600,60},.pos={0,0,0},
                .font_size=32.f,.color=Color{1,1,1,1},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        // Card subtitle
        s.layer("card_sub",[](LayerBuilder& l){
            l.position({0,140,0});
            l.text("t",{
                .text="Glassmorphism with luminous glow border",
                .size={600,40},.pos={0,0,0},
                .font_size=16.f,.color=Color{0.60f,0.72f,0.88f,1},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 5 — Pulse Wave: animated ring + text, pulsating in sync
// ─────────────────────────────────────────────────────────────────────────────
Composition glow_pulse_wave() {
    return composition({.name="GlowPulseWave",.width=W,.height=H,.duration=120},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.01f,0.02f,0.04f,1}, Color{0.02f,0.04f,0.08f,1});

        const float t  = static_cast<float>(ctx.frame) / 120.f;
        const float p  = 0.5f + 0.5f * std::sinf(t * 6.2831853f * 2.f);
        const float p2 = 0.5f + 0.5f * std::sinf(t * 6.2831853f * 2.f + 1.0f);

        // Outer ring glow
        s.layer("ring_outer",[p](LayerBuilder& l){
            const float rad = 160.f + p * 30.f;
            const float inten = 0.4f + p * 0.5f;
            l.position({0,0,0})
             .glow(rad, inten, Color{0.22f,0.80f,1.f,1.f});
            l.circle("c",{.radius=160.f,.color=Color{0,0,0,0},.pos={0,0,0}});
        });

        // Middle ring
        s.layer("ring_mid",[p2](LayerBuilder& l){
            const float rad=80.f+p2*20.f;
            l.position({0,0,0})
             .glow(rad,0.8f+p2*0.4f,Color{0.60f,0.30f,1.f,1.f});
            l.circle("c",{.radius=100.f,.color=Color{0,0,0,0},.pos={0,0,0}});
        });

        // Core orb
        s.layer("core",[p](LayerBuilder& l){
            const float r=36.f+p*8.f;
            l.position({0,0,0})
             .glow(28.f, 1.5f+p*0.4f, Color{0.88f,0.96f,1.f,1.f});
            l.circle("c",{.radius=r,.color=Color{1,1,1,0.95f},.pos={0,0,0}});
        });

        // Pulsing text
        s.layer("text",[p](LayerBuilder& l){
            l.position({0,280,0})
             .glow(16.f, 0.7f+p*0.5f, Color{0.22f,0.80f,1.f,1.f});
            l.text("t",{
                .text="PULSE WAVE",
                .size={700,60},.pos={0,0,0},
                .font_size=36.f,.color=Color{0.7f,0.92f,1.f,1.f},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 6 — Cyberpunk City: video-style scene with multiple glow layers
// ─────────────────────────────────────────────────────────────────────────────
Composition glow_video_source_asset() {
    return composition({.name="GlowVideoSourceAsset",.width=404,.height=244,.duration=120},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        const float t=static_cast<float>(ctx.frame)/120.f;
        const float wave=0.5f+0.5f*std::sinf(t*6.2831853f*2.f);

        s.layer("sky",[](LayerBuilder& l){
            l.rect("bg",{
                .size={404,244},.color=Color{1,1,1,1},
                .pos={-202.f,-122.f,0},
                .fill=Fill::linear({0,0},{0,1},{
                    {0.f,Color{0.11f,0.13f,0.23f,1}},
                    {0.55f,Color{0.10f,0.11f,0.18f,1}},
                    {1.f,Color{0.03f,0.04f,0.08f,1}}
                })
            });
        });

        s.layer("buildings",[wave](LayerBuilder& l){
            const float heights[10]={120,88,144,104,164,128,180,98,150,112};
            const float widths[10]={30,24,26,22,28,24,30,22,26,24};
            float x=-188.f;
            for(int i=0;i<10;++i){
                const float h=heights[i],w=widths[i];
                l.rect("b"+std::to_string(i),{
                    .size={w,h},.color=Color{0.08f,0.10f,0.16f,1},
                    .pos={x,122.f-h,0}
                });
                for(int row=0;row<7;++row)for(int col=0;col<2;++col){
                    const bool lit=((i+row+col+(int)(wave*4))%3)==0;
                    if(lit) l.rect("w"+std::to_string(i*100+row*10+col),{
                        .size={4.f,6.f},.color=Color{0.84f,0.55f,0.84f,1},
                        .pos={x+5.f+(float)col*9.f,122.f-h+10.f+(float)row*14.f,0}
                    });
                }
                x+=w+6.f;
            }
        });

        s.layer("car",[wave](LayerBuilder& l){
            const float bob=(wave-0.5f)*4.f;
            l.position({0,42.f+bob,0}).glow(12.f,0.60f,Color{0.25f,0.70f,1,1});
            l.rect("body",{.size={58,24},.color=Color{0.06f,0.07f,0.10f,1},.pos={-29,-12,0}});
            l.rounded_rect("roof",{.size={38,18},.radius=6,.color=Color{0.05f,0.05f,0.08f,1},.pos={-19,-22,0}});
            l.rect("tl",{.size={12,4},.color=Color{1,.2f,.3f,1},.pos={-24,-8,0}});
            l.rect("tr",{.size={12,4},.color=Color{1,.2f,.3f,1},.pos={12,-8,0}});
        });

        return s.build();
    });
}

Composition glow_video_layer() {
    return composition({.name="GlowVideoLayer",.width=W,.height=H,.duration=120},[](const FrameContext& ctx){
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.01f,0.02f,0.05f,1}, Color{0.03f,0.03f,0.08f,1});

        // Ambient city glow in background
        s.layer("city_amb",[](LayerBuilder& l){
            l.position({0,100,0})
             .glow(300.f,0.4f,Color{0.20f,0.40f,1.f,1.f});
            l.rect("r",{.size={1200,2},.color=Color{0,0,0,0},.pos={-600,0,0}});
        });

        // Video card with cyan glow
        video::VideoSource source;
        source.path="assets/videos/glow_test.mp4";
        source.size={404.f,244.f};
        s.video_layer("video_glow",source,[](LayerBuilder& l){
            l.position({0,-30,0})
             .glow(28.f,0.90f,Color{0.25f,0.72f,1.f,1.f})
             .drop_shadow({0,12},Color{0,0,0,0.60f},18.f);
        });

        // Title above video
        s.layer("vid_title",[](LayerBuilder& l){
            l.position({0,-360,0})
             .glow(18.f,0.9f,Color{0.22f,0.72f,1.f,1.f});
            l.text("t",{
                .text="VIDEO LAYER GLOW",
                .size={800,70},.pos={0,0,0},
                .font_size=40.f,.color=Color{0.7f,0.90f,1.f,1.f},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        // Subtitle below
        s.layer("vid_sub",[](LayerBuilder& l){
            l.position({0,330,0});
            l.text("t",{
                .text="Glow wraps live video content — source_alpha correctly propagated",
                .size={1000,40},.pos={0,0,0},
                .font_size=18.f,.color=Color{0.50f,0.70f,0.88f,1.f},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        return s.build();
    });
}

// ─────────────────────────────────────────────────────────────────────────────

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowNeonSign",       chronon3d::content::effects::glow_neon_sign)
CHRONON_REGISTER_COMPOSITION("GlowOrbGalaxy",      chronon3d::content::effects::glow_orb_galaxy)
CHRONON_REGISTER_COMPOSITION("GlowCinematicText",  chronon3d::content::effects::glow_cinematic_text)
CHRONON_REGISTER_COMPOSITION("GlowUICard",         chronon3d::content::effects::glow_ui_card)
CHRONON_REGISTER_COMPOSITION("GlowPulseWave",      chronon3d::content::effects::glow_pulse_wave)
CHRONON_REGISTER_COMPOSITION("GlowVideoLayer",     chronon3d::content::effects::glow_video_layer)
CHRONON_REGISTER_COMPOSITION("GlowVideoSourceAsset", chronon3d::content::effects::glow_video_source_asset)
