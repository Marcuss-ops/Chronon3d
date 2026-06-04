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

const std::string IMAGE_PATH = "assets/images/camera_reference.jpg";
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

// 2. Image Focus-In
Composition minimalist_image_focus_in() {
    return composition({.name = "MinimalistImageFocusIn", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("image_layer", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.focus_in(24.0f, Frame{45});
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

} // namespace chronon3d::content::minimalist

CHRONON_REGISTER_COMPOSITION("MinimalistImageFadeIn", chronon3d::content::minimalist::minimalist_image_fade_in)
CHRONON_REGISTER_COMPOSITION("MinimalistImageFocusIn", chronon3d::content::minimalist::minimalist_image_focus_in)
CHRONON_REGISTER_COMPOSITION("MinimalistImageScaleDrop", chronon3d::content::minimalist::minimalist_image_scale_drop)
CHRONON_REGISTER_COMPOSITION("MinimalistImageFadeShiftVertical", chronon3d::content::minimalist::minimalist_image_fade_shift_vertical)
CHRONON_REGISTER_COMPOSITION("MinimalistImageCenterSplit", chronon3d::content::minimalist::minimalist_image_center_split)
CHRONON_REGISTER_COMPOSITION("MinimalistImageRevealFromBottom", chronon3d::content::minimalist::minimalist_image_reveal_from_bottom)
