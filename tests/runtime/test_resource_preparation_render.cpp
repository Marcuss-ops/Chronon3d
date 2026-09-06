// test_resource_preparation_render.cpp — Split from
// test_resource_preparation.cpp: prepare_render orchestration coverage
// (preflight failure promotion, recovery after a failure, and rejection
// of a missing renderer).

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/test_utils.hpp>

#include <optional>
#include <string>

TEST_CASE("prepare_render promotes preflight failures to structured errors") {
    auto renderer = chronon3d::test::make_renderer_shared();
    const chronon3d::Composition missing_asset(
        chronon3d::CompositionSpec{
            .name = "missing-asset",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            scene.layer("missing-image", [](chronon3d::LayerBuilder& layer) {
                layer.image("image", {
                    .asset_path = "assets/missing.png",
                    .size = {64.0f, 64.0f},
                });
            });
            return scene.build();
        });

    const auto result = chronon3d::runtime::prepare_render(
        renderer.get(), missing_asset,
        chronon3d::runtime::RenderPreparationOptions{
            .warmup_renderer = false,
        });

    CHECK_FALSE(result.ok());
    REQUIRE(result.preparation_error.has_value());
    CHECK(result.preparation_error->code ==
          chronon3d::runtime::PreparationError::Code::PreflightFailed);
    CHECK(result.preparation_error->phase == "preflight");
    CHECK(result.preparation_error->path == "assets/missing.png");
    CHECK(result.preparation_error->owner == "missing-image/image");
    CHECK(result.preparation_error->cause_code == "ASSET_NOT_FOUND");
    CHECK(result.preparation_error->message.find("Asset not found") != std::string::npos);
    CHECK(result.prepared_assets == std::nullopt);
    CHECK(result.diagnostic().find("ASSET_NOT_FOUND") != std::string::npos);
    CHECK(result.diagnostic().find("assets/missing.png") != std::string::npos);
}

TEST_CASE("prepare_render recovers after a preflight failure") {
    auto renderer = chronon3d::test::make_renderer_shared();
    const chronon3d::Composition missing_asset(
        chronon3d::CompositionSpec{
            .name = "missing-asset",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            scene.layer("missing-image", [](chronon3d::LayerBuilder& layer) {
                layer.image("image", {
                    .asset_path = "assets/missing.png",
                    .size = {64.0f, 64.0f},
                });
            });
            return scene.build();
        });
    const chronon3d::Composition valid(
        chronon3d::CompositionSpec{
            .name = "valid-color",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx);
            scene.layer("color", [](chronon3d::LayerBuilder& layer) {
                layer.rect("background", {
                    .size = {64.0f, 64.0f},
                    .color = chronon3d::Color::white(),
                });
            });
            return scene.build();
        });

    const auto failed = chronon3d::runtime::prepare_render(
        renderer.get(), missing_asset,
        chronon3d::runtime::RenderPreparationOptions{.warmup_renderer = false});
    REQUIRE_FALSE(failed.ok());
    REQUIRE(failed.preparation_error.has_value());
    CHECK(failed.preparation_error->code ==
          chronon3d::runtime::PreparationError::Code::PreflightFailed);

    const auto recovered = chronon3d::runtime::prepare_render(
        renderer.get(), valid,
        chronon3d::runtime::RenderPreparationOptions{.warmup_renderer = false});
    CHECK(recovered.ok());
    CHECK_FALSE(recovered.preparation_error.has_value());
    CHECK(recovered.prepared_assets.has_value());
}

TEST_CASE("prepare_render rejects a missing renderer before execution") {
    const chronon3d::Composition composition(
        chronon3d::CompositionSpec{
            .name = "preparation-test",
            .width = 64,
            .height = 64,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = chronon3d::Frame{1},
        },
        [](const chronon3d::FrameContext&) {
            return chronon3d::Scene{};
        });

    const auto result = chronon3d::runtime::prepare_render(
        nullptr, composition,
        chronon3d::runtime::RenderPreparationOptions{
            .warmup_renderer = false,
        });

    CHECK_FALSE(result.ok());
    REQUIRE(result.preparation_error.has_value());
    CHECK(result.preparation_error->code ==
          chronon3d::runtime::PreparationError::Code::InternalError);
    CHECK(result.preparation_error->phase == "setup");
}
