#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include "compositions/animation_compositions.hpp"
#include "compositions/text_animations.hpp"
#include "compositions/cinematic_showcase.hpp"
#include "compositions/cinematic_title_reveal.hpp"

namespace chronon3d {

class AnimsModule : public ExtensionModule {
public:
    std::string_view id() const override { return "anims"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::anims;

        // Basic animation compositions
        registry.register_composition("AnimFadeInText",  anim_fade_in_text);
        registry.register_composition("AnimSlideText",   anim_slide_text);
        registry.register_composition("AnimScaleText",   anim_scale_text);
        registry.register_composition("AnimTypewriter",  anim_typewriter);

        // Text animation group — 5 easy animations
        registry.register_composition("AnimSlideUp",     anim_slide_up);
        registry.register_composition("AnimScalePop",    anim_scale_pop);
        registry.register_composition("AnimBlurFocus",   anim_blur_focus);
        registry.register_composition("AnimSlideLeft", anim_slide_left);
        registry.register_composition("AnimBounceDrop",  anim_bounce_drop);

        // Text animation group — 5 typewriter animations
        registry.register_composition("AnimTypewriterSimple",  anim_typewriter_simple);
        registry.register_composition("AnimTypewriterCursor",  anim_typewriter_cursor);
        registry.register_composition("AnimTypewriterSlide",   anim_typewriter_slide);
        registry.register_composition("AnimTypewriterGlow",    anim_typewriter_glow);
        registry.register_composition("AnimTypewriterStagger", anim_typewriter_stagger);

        // Cinematic showcase compositions
        registry.register_composition("CatmullRomShowcase",      catmull_rom_showcase);
        registry.register_composition("DollyZoomShowcase",       dolly_zoom_showcase);
        registry.register_composition("CameraSplineComparison",  camera_spline_comparison);

        // Cinematic title reveal
        registry.register_composition("TiltSweepTitle",          tilt_sweep_title);
        registry.register_composition("TiltSweepTitleV2",       tilt_sweep_title_v2);
    }
};

std::unique_ptr<ExtensionModule> create_anims_module() {
    return std::make_unique<AnimsModule>();
}

} // namespace chronon3d
