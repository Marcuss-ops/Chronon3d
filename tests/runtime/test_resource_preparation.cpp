// ═══════════════════════════════════════════════════════════════════════════
// tests/runtime/test_resource_preparation.cpp — TICKET-ASSET-PREP-BARRIER
//
// Locks the canonical 5-phase resource-preparation barrier contract:
//
//   1. Missing required asset → fail-loud with structured
//      `PreparationError{ .code = MissingAsset, ... }` BEFORE encoder.
//   2. All assets present → return `PreparedAssets` with 5 keyed maps.
//   3. Empty manifest → empty PreparedAssets (default-options).
//   4. `WarnAndSkip` policy → diagnostics.warnings populated per asset;
//      PreparedAssets still returned (no abort).
//   5. Per-phase opt-out → matching phase map is empty after preparation.
//   6. `UnresolvableAssetPath` (empty path) → fail-loud error code.
//   7. Each per-phase loader function (font/image/video/audio/layout)
//      returns the right structured error on a missing resolver hit.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/test_utils.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace {

// ── Mock AssetResolver — resolves only the asset paths in `present_set` ──
//
// Default constructor returns a resolver that has NO assets present (all
// `resolve()` calls return std::nullopt → fail-loud path). Tests construct
// with a string set to opt-in to specific paths.
class MockResolver : public chronon3d::assets::AssetResolver {
public:
    MockResolver() = default;
    explicit MockResolver(std::unordered_set<std::string> present)
        : m_present(std::move(present)) {}

    [[nodiscard]] std::optional<std::filesystem::path>
    resolve(const std::filesystem::path& path) const override {
        const auto it = m_present.find(path);
        if (it == m_present.end()) return std::nullopt;
        return std::filesystem::path{*it};
    }

private:
    std::unordered_set<std::string> m_present;
};

} // namespace

TEST_CASE("ResourcePreparation::prepare — empty manifest → empty PreparedAssets (default policy)") {
    chronon3d::assets::AssetManifest manifest;
    MockResolver                    resolver;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver);

    CHECK(result.has_value());
    if (!result.has_value()) return;
    CHECK(result.value().empty());
    CHECK(result.value().diagnostics.warnings.empty());
    CHECK(result.value().diagnostics.fonts_loaded == 0);
    CHECK(result.value().diagnostics.images_decoded == 0);
}

TEST_CASE("ResourcePreparation::prepare — missing required font → fail-loud with structured error") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("missing/font.ttf", "owner/font/missing");

    // Resolver has NO assets present.
    MockResolver                    resolver;
    chronon3d::runtime::PreparationOptions options;  // defaults: FailLoud + all phases ON

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code ==
          chronon3d::runtime::PreparationError::Code::MissingAsset);
    CHECK(result.error().path == "missing/font.ttf");
    CHECK(result.error().owner == "owner/font/missing");
    CHECK(result.error().phase == "font");
    CHECK(!result.error().message.empty());
}

TEST_CASE("ResourcePreparation::prepare — all assets present → success, 4 keyed maps populated") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font.ttf",         "owner/font");
    manifest.add_image("present/image.png",       "owner/image");
    manifest.add_video("present/video.mp4",       "owner/video");
    manifest.add_audio("present/audio.wav",       "owner/audio");

    MockResolver resolver(std::unordered_set<std::string>{
        "present/font.ttf", "present/image.png",
        "present/video.mp4", "present/audio.wav"
    });
    chronon3d::runtime::PreparationOptions options;
    // layout_preparation is keyed by Image kind (placeholder); opt it OUT
    // for this test so we don't double-count images via layout phase.
    options.prepare_layouts = false;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code ==
          chronon3d::runtime::PreparationError::Code::InternalError);
    CHECK(result.error().phase == "video");
    return;
#else
    REQUIRE(result.has_value());
    CHECK(result.value().fonts.count("owner/font")   == 1);
    CHECK(result.value().images.count("owner/image") == 1);
    CHECK(result.value().video_metadata.count("owner/video") == 1);
    CHECK(result.value().audio_index.count("owner/audio")    == 1);
    CHECK(result.value().diagnostics.warnings.empty());
    CHECK(result.value().diagnostics.fonts_loaded   == 1);
    CHECK(result.value().diagnostics.images_decoded == 1);
    CHECK(result.value().diagnostics.video_metadata_probed == 1);
    CHECK(result.value().diagnostics.audio_indexes_built    == 1);
    CHECK(result.value().diagnostics.layouts_prepared       == 0);
#endif
}

TEST_CASE("ResourcePreparation::prepare — WarnAndSkip policy → diagnostics.warnings populated, no abort") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("missing/font.ttf", "owner/font/missing");

    MockResolver                    resolver;  // nothing present
    chronon3d::runtime::PreparationOptions options;
    options.failure_mode =
        chronon3d::runtime::PreparationOptions::FailureMode::WarnAndSkip;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    CHECK(result.value().fonts.empty());
    CHECK(result.value().diagnostics.warnings.size() == 1);
    CHECK(result.value().diagnostics.warnings[0].code ==
          chronon3d::runtime::PreparationError::Code::MissingAsset);
    CHECK(result.value().diagnostics.warnings[0].phase == "font");
}

TEST_CASE("ResourcePreparation::prepare — empty path → UnresolvableAssetPath fail-loud") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("", "owner/font/empty");
    MockResolver                    resolver;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code ==
          chronon3d::runtime::PreparationError::Code::UnresolvableAssetPath);
    CHECK(result.error().path.empty());
    CHECK(result.error().owner == "owner/font/empty");
    CHECK(result.error().phase == "font");
}

TEST_CASE("ResourcePreparation::prepare — per-phase opt-out disables mapping") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font.ttf",   "owner/font");
    manifest.add_image("present/image.png", "owner/image");

    MockResolver resolver(std::unordered_set<std::string>{
        "present/font.ttf", "present/image.png"
    });
    chronon3d::runtime::PreparationOptions options;
    options.prepare_fonts  = false;  // skip phase 1
    options.prepare_images = false;  // skip phase 2

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    CHECK(result.value().fonts.empty());   // phase disabled
    CHECK(result.value().images.empty());  // phase disabled
    CHECK(result.value().diagnostics.warnings.empty());
    CHECK(result.value().diagnostics.fonts_loaded   == 0);
    CHECK(result.value().diagnostics.images_decoded == 0);
}

TEST_CASE("ResourcePreparation — per-phase loader functions lock structured errors") {
    MockResolver resolver;  // nothing present

    const chronon3d::assets::InternalAssetRef bad_font{
        chronon3d::assets::AssetKind::Font,
        "missing/font.ttf",
        "owner/missing",
        /*required=*/true
    };

    SUBCASE("load_font returns MissingAsset") {
        auto r = chronon3d::runtime::ResourcePreparation::load_font(bad_font, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code ==
              chronon3d::runtime::PreparationError::Code::MissingAsset);
        CHECK(r.error().phase == "font");
    }

    SUBCASE("decode_image returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_image{
            chronon3d::assets::AssetKind::Image, "missing/image.png", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::decode_image(bad_image, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "image");
    }

    SUBCASE("probe_video_metadata returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_video{
            chronon3d::assets::AssetKind::Video, "missing/video.mp4", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::probe_video_metadata(
            bad_video, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "video");
    }

    SUBCASE("build_audio_index returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_audio{
            chronon3d::assets::AssetKind::Audio, "missing/audio.wav", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::build_audio_index(
            bad_audio, resolver);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "audio");
    }

    SUBCASE("prepare_layout returns MissingAsset") {
        const chronon3d::assets::InternalAssetRef bad_layout{
            chronon3d::assets::AssetKind::Image, "missing/layout.png", "owner/missing", true
        };
        auto r = chronon3d::runtime::ResourcePreparation::prepare_layout(
            bad_layout, resolver, chronon3d::Frame{0});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().phase == "layout");
    }
}

TEST_CASE("ResourcePreparation::prepare is idempotent") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font.ttf", "owner/font");

    MockResolver resolver(std::unordered_set<std::string>{"present/font.ttf"});
    chronon3d::runtime::PreparationOptions options;
    options.prepare_layouts = false;

    const auto first = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);
    const auto second = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first.value().fonts.size() == second.value().fonts.size());
    CHECK(first.value().fonts.at("owner/font").path ==
          second.value().fonts.at("owner/font").path);
    CHECK(first.value().diagnostics.fonts_loaded ==
          second.value().diagnostics.fonts_loaded);
    CHECK(first.value().diagnostics.warnings.size() ==
          second.value().diagnostics.warnings.size());
}

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
                    .path = "assets/missing.png",
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
                    .path = "assets/missing.png",
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

TEST_CASE("ResourcePreparation::prepare — keyed-by-owner maps (1 owner → 1 entry even with duplicates)") {
    chronon3d::assets::AssetManifest manifest;
    manifest.add_font("present/font1.ttf", "owner/font");
    manifest.add_font("present/font1.ttf", "owner/font");  // duplicate owner
    manifest.add_font("present/font1.ttf", "owner/font");  // duplicate owner

    MockResolver resolver(std::unordered_set<std::string>{"present/font1.ttf"});
    chronon3d::runtime::PreparationOptions options;
    options.prepare_layouts = false;

    auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);

    REQUIRE(result.has_value());
    // owner-keyed: 1 owner → 1 entry, even if asset path appears 3 times.
    CHECK(result.value().fonts.count("owner/font") == 1);
    CHECK(result.value().diagnostics.fonts_loaded == 1);
    CHECK(result.value().diagnostics.warnings.empty());
}
