// tests/visual/glow_ab/glow_ab_compositions.cpp
//
// BUG 2 / TICKET-TEXT-GLOW-DARKENING — Step 3 (A/B test).  See header.
//
// IMPORTANT: this file does NOT modify any production composition.  It
// defines a SIBLING composition that calls build_2line_typewriter with
// identical parameters to `anim_typewriter_glow` except glow_intensity.
// The existing AnimTypewriterGlow composition in
// content/examples/text/text_animations.cpp is left untouched.

#include "glow_ab_compositions.hpp"

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/common/background_helpers.hpp"
#include "content/common/text_reveal_helpers.hpp"

#include <filesystem>

namespace chronon3d::test::glow_ab {

namespace {

// ── Mirror of the static FontEngine pattern from text_animations.cpp ──
//
// `anim_typewriter_glow` uses `shared_typewriter_engine()` defined in the
// anonymous namespace of text_animations.cpp.  We replicate the pattern
// here (local static) so the sibling composition has its own resolver
// and engine, mounted at the project assets root.
chronon3d::FontEngine& no_glow_engine() {
    static chronon3d::assets::AssetResolver s_resolver;
    if (!s_resolver.has_mount()) {
        s_resolver.mount(std::filesystem::current_path());
    }
    static chronon3d::FontEngine s_engine(s_resolver);
    return s_engine;
}

// Constants copied verbatim from text_animations.cpp (the production file
// keeps these in an anonymous namespace, so we replicate them here to
// maintain pixel-parity with `anim_typewriter_glow` except for glow).
constexpr f32 BOX_W = 1400.0f;
constexpr f32 BOX_H = 130.0f;
constexpr f32 BASE_Y = -50.0f;
constexpr f32 TRACKING = 4.0f;

void add_bg(chronon3d::SceneBuilder& s) {
    chronon3d::content::backgrounds::add_common_background(
        s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
}

} // anonymous namespace

Composition make_anim_typewriter_glow_no_glow() {
    return composition({.name = "AnimTypewriterGlowNoGlow",
                        .width = 1920,
                        .height = 1080,
                        .duration = 160},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_bg(s);
        s.font_engine(&no_glow_engine());

        // Same scene as anim_typewriter_glow() but glow_intensity=0.0f.
        // All other parameters (text, sizes, line_spacing, start_delay_2,
        // slide_up=false) are identical to the production composition.
        chronon3d::content::text_reveal::build_2line_typewriter(
            s,
            "THIS TEXT APPEARS",    88.0f,
            "ONE LETTER AT A TIME", 104.0f,
            68.0f,   // start_delay_2 (line 2 starts later) — matches production
            120.0f,  // line_spacing — matches production
            false,   // slide_up — matches production
            0.0f);   // glow_intensity — THE ONLY DIFFERENCE (production: 0.5f)

        return s.build();
    });
}

void register_glow_ab_compositions(CompositionRegistry& registry) {
    registry.add("AnimTypewriterGlowNoGlow",
        [](const CompositionProps&) { return make_anim_typewriter_glow_no_glow(); });
}

} // namespace chronon3d::test::glow_ab
