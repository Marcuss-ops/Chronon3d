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

#include "content/common/background_helpers.hpp"
#include "content/common/text/typewriter_block.hpp"

#include <filesystem>

namespace chronon3d::test::glow_ab {

namespace {

// F0.2 — no_glow_engine() + current_path() static resolver REMOVED.
// The A/B composition now uses ctx.font_engine from the runtime chain:
//   RenderEngine::set_assets_root() → Runtime::resolver() → FontEngine → FrameContext

using chronon3d::content::text_reveal::build_2line_typewriter;

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
        if (ctx.runtime && ctx.runtime->font_engine()) {
            s.font_engine(ctx.runtime->font_engine());
        }

        // Same scene as anim_typewriter_glow() but glow_intensity=0.0f.
        // All other parameters (text, sizes, line_spacing, start_delay_2,
        // slide_up=false) are identical to the production composition.
        build_2line_typewriter(
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
    registry.add(CompositionDescriptor{.id = "AnimTypewriterGlowNoGlow", .factory = [](const CompositionProps&) { return make_anim_typewriter_glow_no_glow(); }});
}

} // namespace chronon3d::test::glow_ab
