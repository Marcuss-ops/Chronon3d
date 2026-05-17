#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <optional>

using namespace chronon3d;

namespace {
SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());
    return renderer;
}

std::string make_white_image_asset() {
    const std::filesystem::path dir = "output/debug/render_graph_unified";
    std::filesystem::create_directories(dir);
    const auto path = dir / "white_asset.png";
    if (std::filesystem::exists(path)) {
        return path.string();
    }

    Framebuffer fb(64, 64);
    fb.clear(Color::white());
    CHECK(save_png(fb, path.string()));
    return path.string();
}

struct RenderResult {
    std::shared_ptr<Framebuffer> fb;
    Transform layer_transform{};
    std::optional<renderer::BrightRegion2D> bbox;
    std::optional<Vec2> centroid;
};

RenderResult render_single_element_scene([[maybe_unused]] const std::string& type_name,
                                         const std::function<void(LayerBuilder&)>& builder_fn,
                                         int width, int height,
                                         const std::optional<Camera2_5D>& camera = std::nullopt) {
    SceneBuilder s;

    s.layer("content", [&](LayerBuilder& l) {
        builder_fn(l);
    });

    auto scene = s.build();

    SoftwareRenderer renderer = make_renderer();
    auto fb = renderer.render_scene(scene, camera, width, height);
    REQUIRE(fb != nullptr);

    RenderResult out;
    out.fb = fb;
    out.layer_transform = scene.layers().front().transform;
    out.bbox = renderer::bright_bbox(*fb);
    out.centroid = renderer::bright_centroid(*fb);
    return out;
}

} // namespace

TEST_CASE("Unified transform path: Rect, Image, and Text share the same base layer matrix") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping unified transform test because font fixture is missing");
        return;
    }

    const std::string white_image = make_white_image_asset();


    const int width = 640;
    const int height = 360;
    const Vec3 center{0.0f, 0.0f, 0.0f};

    struct TransformSpec {
        std::string name;
        std::function<void(LayerBuilder&)> apply;
    };

    const std::array<TransformSpec, 4> transforms = {{
        {"identity", [=](LayerBuilder& l) {
            l.position(center);
        }},
        {"translate", [=](LayerBuilder& l) {
            l.position(center + Vec3{100.5f, 50.25f, 0.0f});
        }},
        {"scale_nonuniform", [=](LayerBuilder& l) {
            l.position(center);
            l.scale({2.0f, 0.5f, 1.0f});
        }},
        {"rotate_15", [=](LayerBuilder& l) {
            l.position(center);
            l.rotate({0.0f, 0.0f, 15.0f});
        }},
    }};

    const std::array<std::string, 3> types = {"rect", "image", "text"};

    for (const auto& transform : transforms) {
        std::optional<Mat4> ref_layer_matrix;
        for (const auto& type : types) {
            auto result = render_single_element_scene(type, [&](LayerBuilder& l) {
                transform.apply(l);
                if (type == "rect") {
                    l.rect("box", {
                        .size = {160.0f, 90.0f},
                        .color = Color::white(),
                        .pos = {0.0f, 0.0f, 0.0f}
                    });
                } else if (type == "image") {
                    l.image("img", {
                        .path = white_image,
                        .size = {160.0f, 90.0f},
                        .pos = {0.0f, 0.0f, 0.0f},
                        .opacity = 1.0f
                    });
                } else {
                    l.text("txt", {
                        .content = "TEST",
                        .style = {
                            .font_path = font,
                            .size = 72.0f,
                            .color = Color::white(),
                            .align = TextAlign::Center
                        },
                        .pos = {0.0f, 0.0f, 0.0f}
                    });
                }
            }, width, height);

            if (!ref_layer_matrix.has_value()) {
                ref_layer_matrix = result.layer_transform.to_mat4();
            } else {
                CHECK(renderer::matrix_near(*ref_layer_matrix, result.layer_transform.to_mat4()));
            }

            CHECK(result.centroid.has_value());
            CHECK(result.bbox.has_value());

            const std::filesystem::path out = std::filesystem::path("output/debug/render_graph_unified") /
                                              ("base_matrix_" + transform.name + "_" + type + ".png");
            CHECK(save_png(*result.fb, out.string()));
            CHECK(std::filesystem::exists(out));
        }
    }
}

TEST_CASE("Unified 2.5D projection: Rect, Image, and Text share the same projected layer matrix") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping unified 2.5D test because font fixture is missing");
        return;
    }

    const std::string white_image = make_white_image_asset();

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -800.0f};
    cam.zoom = 800.0f;

    const int width = 640;
    const int height = 360;

    const std::array<std::pair<std::string, std::function<void(LayerBuilder&)>>, 3> cases = {{
        {"rect", [=](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({30.0f, 0.0f, 0.0f});
            l.rect("box", {
                .size = {160.0f, 90.0f},
                .color = Color::white(),
                .pos = {0.0f, 0.0f, 0.0f}
            });
        }},
        {"image", [=](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({30.0f, 0.0f, 0.0f});
            l.image("img", {
                .path = white_image,
                .size = {160.0f, 90.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .opacity = 1.0f
            });
        }},
        {"text", [=](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({30.0f, 0.0f, 0.0f});
            l.text("txt", {
                .content = "TEST",
                .style = {
                    .font_path = font,
                    .size = 72.0f,
                    .color = Color::white(),
                    .align = TextAlign::Center
                },
                .pos = {0.0f, 0.0f, 0.0f}
            });
        }},
    }};

    std::optional<Mat4> ref_projected_layer;

    for (const auto& [type, fn] : cases) {
        auto result = render_single_element_scene(type, fn, width, height, cam);
        auto projected = project_layer_2_5d(
            result.layer_transform,
            cam,
            static_cast<f32>(width),
            static_cast<f32>(height)
        );

        if (!ref_projected_layer.has_value()) {
            ref_projected_layer = projected.transform.to_mat4();
        } else {
            CHECK(renderer::matrix_near(*ref_projected_layer, projected.transform.to_mat4()));
        }

        CHECK(result.centroid.has_value());
        CHECK(result.bbox.has_value());

        const std::filesystem::path out = std::filesystem::path("output/debug/render_graph_unified") /
                                          ("projection_25d_" + type + ".png");
        CHECK(save_png(*result.fb, out.string()));
        CHECK(std::filesystem::exists(out));
    }
}

TEST_CASE("Unified compositing: z order beats paint order") {
    SoftwareRenderer renderer = make_renderer();
    auto comp = composition({
        .name = "UnifiedZOrder",
        .width = 640,
        .height = 360,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0.0f, 0.0f, -800.0f},
            .zoom = 800.0f
        });

        s.layer("blue_far", [](LayerBuilder& l) {
            l.enable_3d();
            l.position({0.0f, 0.0f, 250.0f});
            l.rect("blue", {
                .size = {520.0f, 320.0f},
                .color = Color{0.0f, 0.0f, 1.0f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        s.layer("red_near", [](LayerBuilder& l) {
            l.enable_3d();
            l.position({0.0f, 0.0f, -250.0f});
            l.rect("red", {
                .size = {420.0f, 260.0f},
                .color = Color{1.0f, 0.0f, 0.0f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(320, 180);
    CHECK(center.r > 0.6f);
    CHECK(center.g < 0.3f);
    CHECK(center.b < 0.3f);

    const std::filesystem::path out = "output/debug/render_graph_unified/z_order.png";
    CHECK(save_png(*fb, out.string()));
    CHECK(std::filesystem::exists(out));
}

TEST_CASE("Unified FakeBox3D: front face matches Rect in shared composition") {
    SoftwareRenderer renderer = make_renderer();
    auto comp = composition({
        .name = "UnifiedFakeBox3DFrontParity",
        .width = 640,
        .height = 360,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0.0f, 0.0f, -800.0f},
            .zoom = 800.0f
        });

        s.layer("ref_rect", [](LayerBuilder& l) {
            l.enable_3d();
            l.rect("ref", {
                .size = {120.0f, 120.0f},
                .color = Color::white(),
                .pos = {-180.0f, 0.0f, 0.0f}
            });
        });

        s.layer("test_box", [](LayerBuilder& l) {
            l.enable_3d();
            l.fake_box3d("box", {
                .pos = {180.0f, 0.0f, 0.0f},
                .size = {120.0f, 120.0f},
                .depth = 0.0f,
                .color = Color::white(),
            });
        });

        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    const std::filesystem::path out = "output/debug/render_graph_unified/fake_box3d_front_parity.png";
    CHECK(save_png(*fb, out.string()));
    CHECK(std::filesystem::exists(out));

    auto bbox = renderer::bright_bbox(*fb);
    auto centroid = renderer::bright_centroid(*fb);
    REQUIRE(bbox.has_value());
    REQUIRE(centroid.has_value());
    CHECK((bbox->max_x - bbox->min_x + 1) > 80);
    CHECK((bbox->max_y - bbox->min_y + 1) > 80);
}

TEST_CASE("Unified FakeExtrudedText: front face matches Text in shared composition") {
    const std::string font = "assets/fonts/Inter-Bold.ttf";
    if (!std::filesystem::exists(font)) {
        MESSAGE("Skipping fake extruded text test because font fixture is missing");
        return;
    }

    SoftwareRenderer renderer = make_renderer();
    auto comp = composition({
        .name = "UnifiedFakeExtrudedTextFrontParity",
        .width = 640,
        .height = 360,
        .duration = 1
    }, [&](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0.0f, 0.0f, -800.0f},
            .zoom = 800.0f
        });

        s.layer("ref_text", [&](LayerBuilder& l) {
            l.enable_3d();
            l.text("ref", {
                .content = "A",
                .style = {
                    .font_path = font,
                    .size = 120.0f,
                    .color = Color::white(),
                    .align = TextAlign::Center
                },
                .pos = {-180.0f, 0.0f, 0.0f}
            });
        });

        s.layer("test_extruded", [&](LayerBuilder& l) {
            l.enable_3d();
            l.fake_extruded_text("extruded", {
                .text = "A",
                .font_path = font,
                .pos = {180.0f, 0.0f, 0.0f},
                .font_size = 120.0f,
                .depth = 0,
                .front_color = Color::white(),
                .side_color = Color::white(),
                .align = TextAlign::Center
            });
        });

        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    const std::filesystem::path out = "output/debug/render_graph_unified/fake_extruded_text_front_parity.png";
    CHECK(save_png(*fb, out.string()));
    CHECK(std::filesystem::exists(out));

    auto bbox = renderer::bright_bbox(*fb);
    auto centroid = renderer::bright_centroid(*fb);
    REQUIRE(bbox.has_value());
    REQUIRE(centroid.has_value());
    CHECK((bbox->max_x - bbox->min_x + 1) > 40);
    CHECK((bbox->max_y - bbox->min_y + 1) > 80);
}
