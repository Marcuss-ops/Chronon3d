#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::minimalist {

// ── Forward declarations (definitions in separate .cpp files) ──────────

Composition minimalist_fade_in();
Composition minimalist_fade_shift_vertical();
Composition minimalist_fade_shift_horizontal();
Composition minimalist_focus_in();
Composition minimalist_reveal_from_bottom();
Composition minimalist_center_split();
Composition minimalist_underline_draw();
Composition minimalist_highlight_block();
Composition minimalist_framing_bracket();
Composition minimalist_word_stagger();
Composition minimalist_scale_drop();
Composition minimalist_tracking_breathing();
Composition minimalist_elegant_exit_vertical();
Composition minimalist_elegant_exit_horizontal();
Composition minimalist_curtain_close();
Composition minimalist_image_fade_in();
Composition minimalist_image_focus_in();
Composition minimalist_image_scale_drop();
Composition minimalist_image_fade_shift_vertical();
Composition minimalist_image_center_split();
Composition minimalist_image_reveal_from_bottom();
Composition minimalist_image_framing_bracket();
Composition minimalist_image_tracking_breathing();
Composition minimalist_image_elegant_exit();
Composition minimalist_image_elastic_slide();
Composition minimalist_focus_quote();
Composition minimalist_phrase_float();
Composition minimalist_hero_explainer();
Composition minimalist_scale_explainer();
Composition minimalist_clean_quote();

} // namespace chronon3d::content::minimalist

namespace chronon3d {

class MinimalistModule : public ExtensionModule {
public:
    std::string_view id() const override { return "minimalist"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::minimalist;

        // Text presets
        registry.register_composition("MinimalistFadeIn", minimalist_fade_in);
        registry.register_composition("MinimalistFadeShiftVertical", minimalist_fade_shift_vertical);
        registry.register_composition("MinimalistFadeShiftHorizontal", minimalist_fade_shift_horizontal);
        registry.register_composition("MinimalistFocusIn", minimalist_focus_in);
        registry.register_composition("MinimalistRevealFromBottom", minimalist_reveal_from_bottom);
        registry.register_composition("MinimalistCenterSplit", minimalist_center_split);
        registry.register_composition("MinimalistUnderlineDraw", minimalist_underline_draw);
        registry.register_composition("MinimalistHighlightBlock", minimalist_highlight_block);
        registry.register_composition("MinimalistFramingBracket", minimalist_framing_bracket);
        registry.register_composition("MinimalistWordStagger", minimalist_word_stagger);
        registry.register_composition("MinimalistScaleDrop", minimalist_scale_drop);
        registry.register_composition("MinimalistTrackingBreathing", minimalist_tracking_breathing);
        registry.register_composition("MinimalistElegantExitVertical", minimalist_elegant_exit_vertical);
        registry.register_composition("MinimalistElegantExitHorizontal", minimalist_elegant_exit_horizontal);
        registry.register_composition("MinimalistCurtainClose", minimalist_curtain_close);

        // Image presets
        registry.register_composition("MinimalistImageFadeIn", minimalist_image_fade_in);
        registry.register_composition("MinimalistImageFocusIn", minimalist_image_focus_in);
        registry.register_composition("MinimalistImageScaleDrop", minimalist_image_scale_drop);
        registry.register_composition("MinimalistImageFadeShiftVertical", minimalist_image_fade_shift_vertical);
        registry.register_composition("MinimalistImageCenterSplit", minimalist_image_center_split);
        registry.register_composition("MinimalistImageRevealFromBottom", minimalist_image_reveal_from_bottom);
        registry.register_composition("MinimalistImageFramingBracket", minimalist_image_framing_bracket);
        registry.register_composition("MinimalistImageTrackingBreathing", minimalist_image_tracking_breathing);
        registry.register_composition("MinimalistImageElegantExit", minimalist_image_elegant_exit);
        registry.register_composition("MinimalistImageElasticSlide", minimalist_image_elastic_slide);

        // Showcase compositions
        registry.register_composition("MinimalistFocusQuote", minimalist_focus_quote);
        registry.register_composition("MinimalistPhraseFloat", minimalist_phrase_float);
        registry.register_composition("MinimalistHeroExplainer", minimalist_hero_explainer);
        registry.register_composition("MinimalistScaleExplainer", minimalist_scale_explainer);
        registry.register_composition("MinimalistCleanQuote", minimalist_clean_quote);
    }
};

std::unique_ptr<ExtensionModule> create_minimalist_module() {
    return std::make_unique<MinimalistModule>();
}

} // namespace chronon3d
