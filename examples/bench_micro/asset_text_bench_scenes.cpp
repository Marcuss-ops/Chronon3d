#include "asset_text_bench_scenes.hpp"

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/presets/scene_presets.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

namespace chronon3d::bench_micro {

using chronon3d::scene_presets::detail::default_font;
using chronon3d::scene_presets::detail::text_preset;

Composition create_image_scene(const std::string& image_path) {
    return composition({
        .name = "MicroBench_Image",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [image_path](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        s.screen_layer("img_layer", [image_path](LayerBuilder& l) {
            l.image("img", {
                .asset_path = image_path,
                .size = {1920.0f, 1080.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .opacity = 1.0f,
            });
        });
        return s.build();
    });
}

Composition create_four_images_scene() {
    return composition({
        .name = "MicroBench_FourImages",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        const std::string root = std::filesystem::current_path().string();
        const std::array<std::string, 4> assets = {
            root + "/assets/images/camera_reference.jpg",
            root + "/assets/images/minimalist_landscape.png",
            root + "/assets/images/checker.png",
            root + "/assets/images/grid_tile.png"
        };
        const std::array<Vec3, 4> positions = {
            Vec3{-480.0f, -270.0f, 0.0f},
            Vec3{ 480.0f, -270.0f, 0.0f},
            Vec3{-480.0f,  270.0f, 0.0f},
            Vec3{ 480.0f,  270.0f, 0.0f}
        };
        for (int i = 0; i < 4; ++i) {
            s.screen_layer("img_" + std::to_string(i), [i, &assets, &positions](LayerBuilder& l) {
                l.image("img", {
                    .asset_path = assets[i],
                    .size = {960.0f, 540.0f},
                    .pos = positions[i],
                    .opacity = 1.0f,
                });
            });
        }
        return s.build();
    });
}

Composition create_15_images_static_scene(const std::string& image_path) {
    return composition({
        .name = "MicroBench_15ImagesStatic",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [image_path](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        for (int i = 0; i < 15; ++i) {
            float x = static_cast<float>((i % 5) * 380 - 760);
            float y = static_cast<float>((i / 5) * 320 - 320);
            s.screen_layer("img_" + std::to_string(i), [image_path, x, y](LayerBuilder& l) {
                l.opacity(0.85f);
                l.image("img", {
                    .asset_path = image_path,
                    .size = {360.0f, 202.0f},
                    .pos = {x, y, 0.0f},
                    .opacity = 1.0f,
                });
            });
        }
        return s.build();
    });
}

Composition create_15_images_animated_scene(const std::string& image_path) {
    return composition({
        .name = "MicroBench_15ImagesAnimated",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [image_path](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        float t = static_cast<float>(ctx.frame().integral()) * 0.05f;
        for (int i = 0; i < 15; ++i) {
            float x = static_cast<float>((i % 5) * 380 - 760) + std::sin(t + static_cast<float>(i)) * 40.0f;
            float y = static_cast<float>((i / 5) * 320 - 320) + std::cos(t + static_cast<float>(i)) * 40.0f;
            s.screen_layer("img_" + std::to_string(i), [image_path, x, y](LayerBuilder& l) {
                l.opacity(0.85f);
                l.image("img", {
                    .asset_path = image_path,
                    .size = {360.0f, 202.0f},
                    .pos = {x, y, 0.0f},
                    .opacity = 1.0f,
                });
            });
        }
        return s.build();
    });
}

Composition create_static_text_scene(const std::string& text, float font_size) {
    return composition({
        .name = "MicroBench_StaticText",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [text, font_size](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        if (ctx.runtime) s.font_engine(&ctx.runtime->font_engine());
        s.screen_layer("text_layer", [text, font_size](LayerBuilder& l) {
            l.text("label", text_preset(
                text, font_size, 700,
                {1.0f, 1.0f, 1.0f, 1.0f},
                TextAlign::Center, {1800.0f, 800.0f}, {0.0f, 0.0f, 0.0f}
            ));
        });
        return s.build();
    });
}

Composition create_dynamic_text_scene(const std::string& base_text) {
    return composition({
        .name = "MicroBench_DynamicText",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [base_text](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        if (ctx.runtime) s.font_engine(&ctx.runtime->font_engine());
        std::string frame_str = base_text + " Frame: " + std::to_string(ctx.frame().integral());
        s.screen_layer("text_layer", [frame_str](LayerBuilder& l) {
            l.text("label", text_preset(
                frame_str, 64.0f, 700,
                {1.0f, 1.0f, 1.0f, 1.0f},
                TextAlign::Center, {1800.0f, 800.0f}, {0.0f, 0.0f, 0.0f}
            ));
        });
        return s.build();
    });
}

Composition create_15_unique_texts_static_scene() {
    return composition({
        .name = "MicroBench_15UniqueTextsStatic",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        if (ctx.runtime) s.font_engine(&ctx.runtime->font_engine());
        for (int i = 0; i < 15; ++i) {
            float x = static_cast<float>((i % 5) * 380 - 760);
            float y = static_cast<float>((i / 5) * 320 - 320);
            std::string label = "Txt_" + std::to_string(i) + "_Static";
            s.screen_layer("txt_" + std::to_string(i), [label, x, y](LayerBuilder& l) {
                l.text("label", text_preset(
                    label, 28.0f, 700,
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    TextAlign::Center, {340.0f, 100.0f}, {x, y, 0.0f}
                ));
            });
        }
        return s.build();
    });
}

Composition create_15_unique_texts_animated_scene() {
    return composition({
        .name = "MicroBench_15UniqueTextsAnimated",
        .width = 1920, .height = 1080,
        .frame_rate = {30, 1},
        .duration = 408,
    }, [](const FrameContext& ctx) -> Scene {
        SceneBuilder s(ctx);
        if (ctx.runtime) s.font_engine(&ctx.runtime->font_engine());
        float t = static_cast<float>(ctx.frame().integral()) * 0.05f;
        for (int i = 0; i < 15; ++i) {
            float x = static_cast<float>((i % 5) * 380 - 760) + std::sin(t + static_cast<float>(i)) * 30.0f;
            float y = static_cast<float>((i / 5) * 320 - 320) + std::cos(t + static_cast<float>(i)) * 30.0f;
            std::string label = "Txt_" + std::to_string(i) + "_Anim";
            s.screen_layer("txt_" + std::to_string(i), [label, x, y](LayerBuilder& l) {
                l.text("label", text_preset(
                    label, 28.0f, 700,
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    TextAlign::Center, {340.0f, 100.0f}, {x, y, 0.0f}
                ));
            });
        }
        return s.build();
    });
}

} // namespace chronon3d::bench_micro
