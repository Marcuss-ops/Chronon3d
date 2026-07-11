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

#include <algorithm>
#include <filesystem>

namespace chronon3d::test::glow_ab {

namespace {

// F0.2 — no_glow_engine() + current_path() static resolver REMOVED.
// The A/B composition now uses ctx.font_engine from the runtime chain:
//   RenderEngine::set_assets_root() → Runtime::resolver() → FontEngine → FrameContext

// Constants copied verbatim from text_animations.cpp (the production file
// keeps these in an anonymous namespace, so we replicate them here to
// maintain pixel-parity with `anim_typewriter_glow` except for glow).
constexpr f32 BASE_Y = -50.0f;
constexpr f32 TRACKING = 4.0f;

using chronon3d::content::animation_helpers::SHADOW_COLOR;
using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::text_reveal::build_text_reveal_line;
using chronon3d::content::text_reveal::font_regular;
using chronon3d::content::text_reveal::measure_text_width;
using chronon3d::content::text_reveal::TextRevealDescriptor;

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
        s.font_engine(ctx.font_engine);

        // Same scene as anim_typewriter_glow() but glow_intensity=0.0f.
        // All other parameters (text, sizes, line_spacing, start_delay_2,
        // slide_up=false) are identical to the production composition.
        // Inlined from text_animations.cpp because build_2line_typewriter is
        // an internal helper in that TU's anonymous namespace.
        constexpr f32 line_spacing = 120.0f;
        constexpr f32 start_delay_2 = 68.0f;
        auto spec = font_regular();
        f32 w1 = measure_text_width("THIS TEXT APPEARS", 88.0f, spec, TRACKING, *s.font_engine());
        f32 w2 = measure_text_width("ONE LETTER AT A TIME", 104.0f, spec, TRACKING, *s.font_engine());
        f32 max_w = std::max(w1, w2);
        f32 ref_x = -max_w * 0.5f;

        auto d1 = TextRevealDescriptor{
            .text = "THIS TEXT APPEARS", .font_size = 88.0f, .font_spec = spec,
            .tracking = TRACKING, .ref_offset_x = ref_x,
            .base_pos = {0.0f, BASE_Y - line_spacing * 0.5f, 0.0f},
            .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
            .slide_up = false, .pin_to_center = true,
            .color = TEXT_COLOR, .add_shadow = true, .shadow_color = SHADOW_COLOR,
            .glow_intensity = 0.0f,
            .layer_prefix = "ch_0"
        };
        build_text_reveal_line(s, d1);

        auto d2 = d1;
        d2.text = "ONE LETTER AT A TIME";
        d2.font_size = 104.0f;
        d2.base_pos = {0.0f, BASE_Y + line_spacing * 0.5f, 0.0f};
        d2.start_delay = start_delay_2;
        d2.layer_prefix = "ch_1";
        build_text_reveal_line(s, d2);

        return s.build();
    });
}

void register_glow_ab_compositions(CompositionRegistry& registry) {
    registry.add("AnimTypewriterGlowNoGlow",
        [](const CompositionProps&) { return make_anim_typewriter_glow_no_glow(); });
}

} // namespace chronon3d::test::glow_ab
