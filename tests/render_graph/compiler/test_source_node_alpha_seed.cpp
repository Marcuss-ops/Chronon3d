#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/compiler/compiler_pass_pipeline.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

class AlphaFixtureBackend final : public chronon3d::image::ImageBackend {
public:
    std::unique_ptr<chronon3d::image::ImageBuffer> load_image(
        const std::string& path) override {
        auto image = std::make_unique<chronon3d::image::ImageBuffer>();
        image->width = 4;
        image->height = 4;
        image->channels = 4;
        const bool opaque = path.find("transparent") == std::string::npos;
        if (path.find("linear") != std::string::npos) {
            image->linear_pixels = std::make_unique<float[]>(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i) {
                image->linear_pixels[i * 4 + 0] = 0.4f;
                image->linear_pixels[i * 4 + 1] = 0.3f;
                image->linear_pixels[i * 4 + 2] = 0.2f;
                image->linear_pixels[i * 4 + 3] = opaque || i != 5 ? 1.0f : 0.0f;
            }
        } else {
            image->pixels = std::make_unique<chronon3d::u8[]>(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i) {
                image->pixels[i * 4 + 0] = 120;
                image->pixels[i * 4 + 1] = 80;
                image->pixels[i * 4 + 2] = 40;
                image->pixels[i * 4 + 3] = opaque || i != 5 ? 255 : 0;
            }
        }
        return image;
    }
};

class AlphaSeedFixture {
public:
    AlphaSeedFixture() {
        root = std::filesystem::temp_directory_path() /
            ("chronon3d_alpha_seed_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        for (const auto* name : {"opaque.png", "transparent.png",
                                 "opaque-linear.exr", "transparent-linear.exr"}) {
            std::ofstream(root / name, std::ios::binary) << "fixture";
        }
        resolver.mount(root);
        cache.set_backend(std::make_shared<AlphaFixtureBackend>());
    }

    ~AlphaSeedFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    chronon3d::assets::AssetResolver resolver;
    chronon3d::ImageCache cache;
};

} // namespace

TEST_CASE("SourceNode seeds full-canvas images only with decoded opaque alpha coverage") {
    AlphaSeedFixture fixture;
    chronon3d::graph::RenderGraphContext context;
    context.frame_input.width = 4;
    context.frame_input.height = 4;
    context.image_cache = &fixture.cache;
    context.asset_resolver = &fixture.resolver;

    const auto can_seed = [&](const std::string& path,
                              float opacity = 1.0f,
                              float tint_alpha = 1.0f,
                              chronon3d::FitMode fit = chronon3d::FitMode::Cover) {
        chronon3d::RenderNode node;
        node.shape = chronon3d::Shape{chronon3d::ImageShape{
            .path = path,
            .size = chronon3d::Vec2{4.0f, 4.0f},
            .fit = fit,
            .opacity = opacity,
            .tint = chronon3d::Color{1.0f, 1.0f, 1.0f, tint_alpha},
        }};
        chronon3d::cache::NodeCacheKey key{};
        chronon3d::graph::SourceNode source(path, node, key);
        return source.can_seed_full_frame(context);
    };
    const auto graph_skips_initial_clear = [&](const std::string& path) {
        chronon3d::RenderNode node;
        node.shape = chronon3d::Shape{chronon3d::ImageShape{
            .path = path,
            .size = chronon3d::Vec2{4.0f, 4.0f},
            .opacity = 1.0f,
        }};
        chronon3d::Scene scene;
        scene.add_node(std::move(node));
        context.policy.skip_initial_clear = false;
        chronon3d::compiler::CompilerPassPipeline pipeline;
        pipeline.add_default_passes();
        const auto graph = pipeline.build(scene, context);
        REQUIRE(graph.size() > 0);
        return context.policy.skip_initial_clear;
    };

    CHECK_FALSE(can_seed("transparent.png")); // no prepared decode yet
    REQUIRE(fixture.cache.get_or_load("transparent.png", {}, fixture.resolver));
    CHECK_FALSE(can_seed("transparent.png"));
    CHECK_FALSE(graph_skips_initial_clear("transparent.png"));
    REQUIRE(fixture.cache.get_or_load("opaque.png", {}, fixture.resolver));
    CHECK(can_seed("opaque.png"));
    CHECK(graph_skips_initial_clear("opaque.png"));
    CHECK_FALSE(can_seed("opaque.png", 1.0f, 0.5f));
    CHECK_FALSE(can_seed("opaque.png", 1.0f, 1.0f, chronon3d::FitMode::Contain));
    REQUIRE(fixture.cache.get_or_load("transparent-linear.exr", {}, fixture.resolver));
    CHECK_FALSE(can_seed("transparent-linear.exr"));
    REQUIRE(fixture.cache.get_or_load("opaque-linear.exr", {}, fixture.resolver));
    CHECK(can_seed("opaque-linear.exr"));
}
