// test_resource_preparation.cpp — TICKET-ASSET-PREP-BARRIER: locks the
// canonical 5-phase resource-preparation barrier contract (fail-loud
// structured PreparationError codes before the encoder, keyed
// PreparedAssets maps, WarnAndSkip diagnostics, per-phase opt-out and
// per-phase loader structured errors).
// Split parts (each ≤ 400 LOC):
//   - test_resource_preparation_mesh.cpp   (GLB / MeshLoader coverage)
//   - test_resource_preparation_render.cpp (prepare_render orchestration)

#include <doctest/doctest.h>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/test_utils.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
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

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

std::filesystem::path write_media_fixture(
    std::string_view extension, const std::vector<std::byte>& bytes) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("chronon3d-preparation-media-" + std::to_string(unique) +
         std::string(extension));
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) throw std::runtime_error("could not create media fixture");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) throw std::runtime_error("could not write media fixture");
    return path;
}

std::filesystem::path write_probe_video_fixture() {
    const std::string header =
        "YUV4MPEG2 W2 H2 F30:1 Ip A0:0 C420jpeg\nFRAME\n";
    std::vector<std::byte> bytes(
        reinterpret_cast<const std::byte*>(header.data()),
        reinterpret_cast<const std::byte*>(header.data() + header.size()));
    // One 2×2 YUV420 frame: 4 luma samples followed by one U and one V.
    bytes.insert(bytes.end(), {
        std::byte{16}, std::byte{16}, std::byte{16}, std::byte{16},
        std::byte{128}, std::byte{128}});
    return write_media_fixture(".y4m", bytes);
}

std::filesystem::path write_probe_audio_fixture() {
    constexpr std::uint16_t channels = 1;
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t data_size = sample_rate * channels * bits_per_sample / 8;
    constexpr std::uint32_t riff_size = 36 + data_size;
    std::vector<std::byte> bytes;
    const auto append_tag = [&bytes](std::string_view tag) {
        bytes.insert(bytes.end(), reinterpret_cast<const std::byte*>(tag.data()),
                     reinterpret_cast<const std::byte*>(tag.data() + tag.size()));
    };
    append_tag("RIFF"); append_u32(bytes, riff_size); append_tag("WAVE");
    append_tag("fmt "); append_u32(bytes, 16); append_u16(bytes, 1);
    append_u16(bytes, channels); append_u32(bytes, sample_rate);
    append_u32(bytes, sample_rate * channels * bits_per_sample / 8);
    append_u16(bytes, channels * bits_per_sample / 8); append_u16(bytes, bits_per_sample);
    append_tag("data"); append_u32(bytes, data_size);
    bytes.resize(bytes.size() + data_size, std::byte{0});
    return write_media_fixture(".wav", bytes);
}

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
    CHECK((result.error().code == chronon3d::runtime::PreparationError::Code::InternalError ||
           result.error().code == chronon3d::runtime::PreparationError::Code::CorruptedAsset));
    CHECK((result.error().phase == "video" || result.error().phase == "image"));
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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
TEST_CASE("ResourcePreparation::prepare propagates probed video/audio metadata") {
    const auto video_path = write_probe_video_fixture();
    const auto audio_path = write_probe_audio_fixture();

    chronon3d::assets::AssetManifest manifest;
    manifest.add_video(video_path.filename().string(), "owner/video-real");
    manifest.add_audio(audio_path.filename().string(), "owner/audio-real");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(video_path.parent_path());

    chronon3d::runtime::PreparationOptions options;
    options.prepare_fonts = false;
    options.prepare_images = false;
    options.prepare_layouts = false;
    options.prepare_meshes = false;

    const auto result = chronon3d::runtime::ResourcePreparation::prepare(
        manifest, resolver, options);
    REQUIRE(result.has_value());

    const auto video = result.value().video_metadata.at("owner/video-real");
    CHECK(video.width == 2);
    CHECK(video.height == 2);
    // F3.1 — rational authority: exact num/den round-trip, no float compare.
    CHECK(video.frame_rate.numerator == 30);
    CHECK(video.frame_rate.denominator == 1);
    CHECK(video.frame_count >= 1);

    const auto audio = result.value().audio_index.at("owner/audio-real");
    CHECK(audio.duration_seconds == doctest::Approx(1.0f));
    CHECK(audio.sample_rate == 8000);

    std::error_code ignored;
    std::filesystem::remove(video_path, ignored);
    std::filesystem::remove(audio_path, ignored);
}
#endif

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
