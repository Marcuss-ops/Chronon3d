#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include "minimalist_animations.hpp"

namespace chronon3d {

class MinimalistModule : public ExtensionModule {
public:
    std::string_view id() const override { return "minimalist"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::minimalist;

        // Text animations
        registry.register_composition("MinimalistTextFadeUp", minimalist_text_fade_up);
        registry.register_composition("MinimalistTextTrackingReveal", minimalist_text_tracking_reveal);
        registry.register_composition("MinimalistTextClipReveal", minimalist_text_clip_reveal);
        registry.register_composition("MinimalistTextFadeDown", minimalist_text_fade_down);
        registry.register_composition("MinimalistTextSoftScale", minimalist_text_soft_scale);
        registry.register_composition("MinimalistTextBlurFocus", minimalist_text_blur_focus);
        registry.register_composition("MinimalistTextSlideLeft", minimalist_text_slide_left);
        registry.register_composition("MinimalistTextSlideRight", minimalist_text_slide_right);
        registry.register_composition("MinimalistTextScalePop", minimalist_text_scale_pop);
        registry.register_composition("MinimalistTextFloatIn", minimalist_text_float_in);
        registry.register_composition("MinimalistTextLetterRise", minimalist_text_letter_rise);
        registry.register_composition("MinimalistTextDriftIn", minimalist_text_drift_in);
        registry.register_composition("MinimalistTextTiltIn", minimalist_text_tilt_in);
        registry.register_composition("MinimalistTextMaskReveal", minimalist_text_mask_reveal);
        registry.register_composition("MinimalistTextSnapPop", minimalist_text_snap_pop);

        // Exit animations
        registry.register_composition("MinimalistTextFadeAway", minimalist_text_fade_away);
        registry.register_composition("MinimalistTextScaleOut", minimalist_text_scale_out);
        registry.register_composition("MinimalistTextSlideUpOut", minimalist_text_slide_up_out);
        registry.register_composition("MinimalistTextBlurAway", minimalist_text_blur_away);
        registry.register_composition("MinimalistTextTiltOut", minimalist_text_tilt_out);

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
