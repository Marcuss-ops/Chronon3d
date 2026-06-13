#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::minimalist {

// Text animations
Composition minimalist_text_fade();
Composition minimalist_text_rise();
Composition minimalist_text_focus();
Composition minimalist_text_scale();
Composition minimalist_text_drift();
Composition minimalist_text_typewriter();
Composition minimalist_text_tw_cascade();
Composition minimalist_text_tw_wave();
Composition minimalist_text_tw_snap();
Composition minimalist_text_tw_drift();
Composition minimalist_text_tw_bold();

// Image presets
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

} // namespace chronon3d::content::minimalist

namespace chronon3d {

class MinimalistModule : public ExtensionModule {
public:
    std::string_view id() const override { return "minimalist"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::minimalist;

        // Text animations
        registry.register_composition("MinimalistTextFade", minimalist_text_fade);
        registry.register_composition("MinimalistTextRise", minimalist_text_rise);
        registry.register_composition("MinimalistTextFocus", minimalist_text_focus);
        registry.register_composition("MinimalistTextScale", minimalist_text_scale);
        registry.register_composition("MinimalistTextDrift", minimalist_text_drift);
        registry.register_composition("MinimalistTextTypewriter", minimalist_text_typewriter);
        registry.register_composition("MinimalistTextTWCascade", minimalist_text_tw_cascade);
        registry.register_composition("MinimalistTextTWWave", minimalist_text_tw_wave);
        registry.register_composition("MinimalistTextTWSnap", minimalist_text_tw_snap);
        registry.register_composition("MinimalistTextTWDrift", minimalist_text_tw_drift);
        registry.register_composition("MinimalistTextTWBold", minimalist_text_tw_bold);

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
    }
};

std::unique_ptr<ExtensionModule> create_minimalist_module() {
    return std::make_unique<MinimalistModule>();
}

} // namespace chronon3d
