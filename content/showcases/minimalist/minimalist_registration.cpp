#include "content/showcases/minimalist/minimalist_image_presets.hpp"
#include "content/showcases/minimalist/minimalist_text_presets.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>

#include <string>
#include <utility>

namespace chronon3d::content::minimalist {
namespace {

constexpr i32 kWidth = 1920;
constexpr i32 kHeight = 1080;
constexpr FrameRate kFps{30, 1};

CompositionDescriptor minimalist_descriptor(
    std::string id,
    Frame duration,
    CompositionRegistry::Factory factory) {
    CompositionDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.category = "Minimalist";
    descriptor.width = kWidth;
    descriptor.height = kHeight;
    descriptor.fps = kFps;
    descriptor.duration = duration;
    descriptor.factory = std::move(factory);
    return descriptor;
}

} // namespace

void register_minimalist_compositions(CompositionRegistry& registry) {
    registry.add(minimalist_descriptor(
        "MinimalistTextFadeUp", Frame{150},
        [](const CompositionProps&) { return minimalist_text_fade_up(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextTrackingReveal", Frame{150},
        [](const CompositionProps&) { return minimalist_text_tracking_reveal(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextClipReveal", Frame{150},
        [](const CompositionProps&) { return minimalist_text_clip_reveal(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextFadeDown", Frame{150},
        [](const CompositionProps&) { return minimalist_text_fade_down(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextSoftScale", Frame{150},
        [](const CompositionProps&) { return minimalist_text_soft_scale(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextBlurFocus", Frame{150},
        [](const CompositionProps&) { return minimalist_text_blur_focus(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextSlideLeft", Frame{150},
        [](const CompositionProps&) { return minimalist_text_slide_left(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextSlideRight", Frame{150},
        [](const CompositionProps&) { return minimalist_text_slide_right(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextScalePop", Frame{150},
        [](const CompositionProps&) { return minimalist_text_scale_pop(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextFloatIn", Frame{150},
        [](const CompositionProps&) { return minimalist_text_float_in(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextFloatInNoGlow", Frame{150},
        [](const CompositionProps&) { return minimalist_text_float_in_no_glow(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextLetterRise", Frame{150},
        [](const CompositionProps&) { return minimalist_text_letter_rise(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextDriftIn", Frame{150},
        [](const CompositionProps&) { return minimalist_text_drift_in(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextTiltIn", Frame{150},
        [](const CompositionProps&) { return minimalist_text_tilt_in(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextMaskReveal", Frame{150},
        [](const CompositionProps&) { return minimalist_text_mask_reveal(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextSnapPop", Frame{150},
        [](const CompositionProps&) { return minimalist_text_snap_pop(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextFadeAway", Frame{150},
        [](const CompositionProps&) { return minimalist_text_fade_away(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextScaleOut", Frame{150},
        [](const CompositionProps&) { return minimalist_text_scale_out(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextSlideUpOut", Frame{150},
        [](const CompositionProps&) { return minimalist_text_slide_up_out(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextBlurAway", Frame{150},
        [](const CompositionProps&) { return minimalist_text_blur_away(); }));
    registry.add(minimalist_descriptor(
        "MinimalistTextTiltOut", Frame{150},
        [](const CompositionProps&) { return minimalist_text_tilt_out(); }));

    registry.add(minimalist_descriptor(
        "MinimalistImageFadeIn", Frame{120},
        [](const CompositionProps&) { return minimalist_image_fade_in(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageFocusIn", Frame{120},
        [](const CompositionProps&) { return minimalist_image_focus_in(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageScaleDrop", Frame{120},
        [](const CompositionProps&) { return minimalist_image_scale_drop(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageFadeShiftVertical", Frame{120},
        [](const CompositionProps&) { return minimalist_image_fade_shift_vertical(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageCenterSplit", Frame{120},
        [](const CompositionProps&) { return minimalist_image_center_split(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageRevealFromBottom", Frame{120},
        [](const CompositionProps&) { return minimalist_image_reveal_from_bottom(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageFramingBracket", Frame{120},
        [](const CompositionProps&) { return minimalist_image_framing_bracket(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageTrackingBreathing", Frame{150},
        [](const CompositionProps&) { return minimalist_image_tracking_breathing(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageElegantExit", Frame{150},
        [](const CompositionProps&) { return minimalist_image_elegant_exit(); }));
    registry.add(minimalist_descriptor(
        "MinimalistImageElasticSlide", Frame{120},
        [](const CompositionProps&) { return minimalist_image_elastic_slide(); }));
}

} // namespace chronon3d::content::minimalist
