#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::minimalist {

namespace {

// Common setup helper for the background grid
void add_common_background(SceneBuilder& s) {
    s.layer("background", [](auto& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.grid_background("grid_bg", {
            .size = {1920.0f, 1080.0f},
            .offset = {0.0f, 0.0f},
            .bg_color = {0.025f, 0.027f, 0.031f, 1.0f},
            .grid_color = {0.58f, 0.61f, 0.66f, 0.045f},
            .spacing = 136.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        });
    });
}

// Common setup helper for image container/border
void add_image_border(LayerBuilder& l, Vec2 size) {
    // Elegant dark frame behind the image
    l.rounded_rect("image_backdrop", {
        .size = size + Vec2{24.0f, 24.0f},
        .radius = 16.0f,
        .color = Color{0.0f, 0.0f, 0.0f, 0.35f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
    // Inner thin border
    l.rounded_rect("image_border", {
        .size = size + Vec2{2.0f, 2.0f},
        .radius = 10.0f,
        .color = Color{0.25f, 0.27f, 0.31f, 0.8f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
}

const std::string IMAGE_PATH = "assets/images/minimalist_landscape.png";
const Vec2 IMAGE_SIZE = {800.0f, 450.0f};

} // namespace

// 1. Image Fade-In
Composition minimalist_image_fade_in() {
    return composition({.name = "MinimalistImageFadeIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_in(Frame{45});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 2. Image Focus-In — animated blur + opacity fade-in + subtle scale pop.
// Blur starts at 8px and animates to 0 over 45 frames with OutCubic easing.
Composition minimalist_image_focus_in() {
    return composition({.name = "MinimalistImageFocusIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            // Opacity fade-in: from transparent to fully visible
            auto& op = l.opacity_anim();
            op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.key(Frame{45}, 1.0f);
            // Blur animation: from blurry to sharp
            auto& bl = l.blur_anim();
            bl.key(Frame{0}, 8.0f, EasingCurve{Easing::OutCubic});
            bl.key(Frame{45}, 0.0f);
            // Subtle scale pop to compensate for blur perception
            auto& sc = l.scale_anim();
            sc.key(Frame{0}, Vec3{1.04f, 1.04f, 1.0f}, EasingCurve{Easing::OutCubic});
            sc.key(Frame{45}, Vec3{1.0f, 1.0f, 1.0f});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 3. Image Scale Drop
Composition minimalist_image_scale_drop() {
    return composition({.name = "MinimalistImageScaleDrop", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.scale_drop(1.08f, Frame{45});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 4. Image Fade & Shift Vertical
Composition minimalist_image_fade_shift_vertical() {
    return composition({.name = "MinimalistImageFadeShiftVertical", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_shift_vertical(Vec3{0.0f, 40.0f, 0.0f}, Frame{45});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 5. Image Center Split
Composition minimalist_image_center_split() {
    return composition({.name = "MinimalistImageCenterSplit", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.center_split(Frame{45});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 6. Image Reveal From Bottom
Composition minimalist_image_reveal_from_bottom() {
    return composition({.name = "MinimalistImageRevealFromBottom", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.reveal_from_bottom(60.0f, Frame{45});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 7. Image Framing Bracket
Composition minimalist_image_framing_bracket() {
    return composition({.name = "MinimalistImageFramingBracket", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.framing_bracket(Frame{45});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 8. Image Tracking & Breathing
Composition minimalist_image_tracking_breathing() {
    return composition({.name = "MinimalistImageTrackingBreathing", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.tracking_breathing(1.04f, Frame{120});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 9. Image Elegant Exit (5 seconds)
Composition minimalist_image_elegant_exit() {
    return composition({.name = "MinimalistImageElegantExit", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            
            // Entrance animation (0 to 45): Fade, scale zoom-in, and slide up
            auto& op = l.opacity_anim();
            op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
            op.key(Frame{45}, 1.0f);
            
            auto& sc = l.scale_anim();
            sc.key(Frame{0}, Vec3{0.92f, 0.92f, 1.0f}, EasingCurve{Easing::OutBack});
            sc.key(Frame{45}, Vec3{1.0f, 1.0f, 1.0f});
            
            auto& pos = l.position_anim();
            pos.key(Frame{0}, Vec3{0.0f, 120.0f, 0.0f}, EasingCurve{Easing::OutBack});
            pos.key(Frame{45}, Vec3{0.0f, 0.0f, 0.0f});
            
            // Exit animation (110 to 150): Slide down off-screen and fade out
            pos.key(Frame{110}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::InCubic});
            pos.key(Frame{150}, Vec3{0.0f, 650.0f, 0.0f});
            
            op.key(Frame{110}, 1.0f, EasingCurve{Easing::OutCubic});
            op.key(Frame{150}, 0.0f);
            
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

// 10. Image Elastic Slide
Composition minimalist_image_elastic_slide() {
    return composition({.name = "MinimalistImageElasticSlide", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.fade_shift_horizontal(Vec3{-120.0f, 0.0f, 0.0f}, Frame{60}, EasingCurve{Easing::OutBack});
            add_image_border(l, IMAGE_SIZE);
            l.image("img", {
                .path = IMAGE_PATH,
                .size = IMAGE_SIZE,
                .radius = 8.0f
            });
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist

// Registrations are now handled by MinimalistModule in minimalist_module.cpp
