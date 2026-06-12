#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

// 1. Image Fade-In
Composition minimalist_image_fade_in() {
    return composition({.name = "MinimalistImageFadeIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_in(Frame{45});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 2. Image Focus-In — animated blur + opacity fade-in + subtle scale pop.
Composition minimalist_image_focus_in() {
    return composition({.name = "MinimalistImageFocusIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            auto& op = l.opacity_anim();
            op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.key(Frame{45}, 1.0f);
            auto& bl = l.blur_anim();
            bl.key(Frame{0}, 8.0f, EasingCurve{Easing::OutCubic});
            bl.key(Frame{45}, 0.0f);
            auto& sc = l.scale_anim();
            sc.key(Frame{0}, Vec3{1.04f, 1.04f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.key(Frame{45}, Vec3{1.0f, 1.0f, 1.0f});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 3. Image Scale Drop
Composition minimalist_image_scale_drop() {
    return composition({.name = "MinimalistImageScaleDrop", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.scale_drop(1.08f, Frame{45});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 4. Image Fade & Shift Vertical
Composition minimalist_image_fade_shift_vertical() {
    return composition({.name = "MinimalistImageFadeShiftVertical", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_shift_vertical(Vec3{0.0f, 40.0f, 0.0f}, Frame{45});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 5. Image Center Split
Composition minimalist_image_center_split() {
    return composition({.name = "MinimalistImageCenterSplit", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.center_split(Frame{45});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 6. Image Reveal From Bottom
Composition minimalist_image_reveal_from_bottom() {
    return composition({.name = "MinimalistImageRevealFromBottom", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.reveal_from_bottom(60.0f, Frame{45});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 7. Image Framing Bracket
Composition minimalist_image_framing_bracket() {
    return composition({.name = "MinimalistImageFramingBracket", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.framing_bracket(Frame{45});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 8. Image Tracking & Breathing
Composition minimalist_image_tracking_breathing() {
    return composition({.name = "MinimalistImageTrackingBreathing", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.tracking_breathing(1.04f, Frame{120});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 9. Image Elegant Exit (5 seconds)
Composition minimalist_image_elegant_exit() {
    return composition({.name = "MinimalistImageElegantExit", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            
            auto& op = l.opacity_anim();
            op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.key(Frame{45}, 1.0f);
            
            auto& sc = l.scale_anim();
            sc.key(Frame{0}, Vec3{0.92f, 0.92f, 1.0f}, EasingCurve{Easing::OutBack});
            sc.key(Frame{45}, Vec3{1.0f, 1.0f, 1.0f});
            
            auto& pos = l.position_anim();
            pos.key(Frame{0}, Vec3{0.0f, 120.0f, 0.0f}, EasingCurve{Easing::OutBack});
            pos.key(Frame{45}, Vec3{0.0f, 0.0f, 0.0f});
            
            pos.key(Frame{110}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::InCubic});
            pos.key(Frame{150}, Vec3{0.0f, 650.0f, 0.0f});
            
            op.key(Frame{110}, 1.0f, EasingCurve{Easing::OutCubic});
            op.key(Frame{150}, 0.0f);
            
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

// 10. Image Elastic Slide
Composition minimalist_image_elastic_slide() {
    return composition({.name = "MinimalistImageElasticSlide", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_minimalist_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_shift_horizontal(Vec3{-120.0f, 0.0f, 0.0f}, Frame{60}, EasingCurve{Easing::OutBack});
            add_image_border(l);
            l.image("img", {.path = IMAGE_PATH, .size = IMAGE_SIZE, .radius = IMAGE_RADIUS});
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registration is intentionally left to the caller; this file only defines the
// standalone image preset compositions.
