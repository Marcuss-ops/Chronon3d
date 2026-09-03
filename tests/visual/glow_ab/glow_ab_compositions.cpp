// tests/visual/glow_ab/glow_ab_compositions.cpp
//
// BUG 2 / TICKET-TEXT-GLOW-DARKENING — Step 3 (A/B test).  See header.
//
// IMPORTANT: this file does NOT modify any production composition.  It
// defines a SIBLING composition that calls build_2line_typewriter with
// identical parameters to `anim_typewriter_glow` except glow_intensity.
// The existing AnimTypewriterGlow composition in the retired
// content/examples/text/text_animations.cpp is left untouched.  // drift-class: historical (examples tree retired)

#include "glow_ab_compositions.hpp"

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

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
        s.font_engine(ctx.runtime ? &ctx.runtime->font_engine() : nullptr);

        // Same scene as anim_typewriter_glow() but glow_intensity=0.0f.
        // All other parameters (text, sizes, line_spacing, start_delay_2,
        // slide_up=false) are identical to the production composition.
        build_2line_typewriter(s, {
            .first  = {.text = "THIS TEXT APPEARS",    .font_size = 88.0f},
            .second = {.text = "ONE LETTER AT A TIME", .font_size = 104.0f},
            .second_delay   = 68.0f,
            .line_spacing   = 120.0f,
            .glow_intensity = 0.0f,
        });

        return s.build();
    });
}

void register_glow_ab_compositions(CompositionRegistry& registry) {
    registry.add(make_composition_descriptor("AnimTypewriterGlowNoGlow", [](const CompositionProps&) { return make_anim_typewriter_glow_no_glow(); }));
}

} // namespace chronon3d::test::glow_ab
