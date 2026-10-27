// =============================================================================
// tests/cache/test_image_cache_display_bound_downscale.cpp
//
// Fix 6 (TICKET-OVERLAY-THROUGHPUT-100-MIN): the runtime copy of a still
// image is bounded by its display box (ImageDecodeOptions::max_dimension,
// declared by the scene builder as 2x the fitted box). Cases here pin:
//
//   * the decode is downscaled to the bound (longest side, aspect kept);
//   * the resampled content keeps the premultiplied-linear average of the
//     source (area-average resampling, no point-sampling aliasing);
//   * images already inside the bound are untouched (byte-stable path);
//   * a different bound is a different content-addressed cache entry, so two
//     display resolutions can never alias one representation.
// =============================================================================

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/image/image_backend.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

using namespace chronon3d;

namespace {

namespace fs = std::filesystem;

struct DisplayBoundFixture {
    DisplayBoundFixture() {
        const auto stamp =
            std::chrono::system_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
               ("chronon3d_test_image_display_bound_" + std::to_string(stamp));
        std::error_code ec;
        fs::create_directories(root, ec);
    }

    ~DisplayBoundFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // Solid premultiplied-linear colour PNG at the requested size. A solid
    // fill makes the area-average invariant exact: any correct box filter
    // preserves the mean, so content assertions do not depend on the kernel.
    std::string solid_png(int width, int height, const std::string& name,
                          float r, float g, float b) const {
        Framebuffer fb(width, height);
        for (int y = 0; y < height; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = 0; x < width; ++x) {
                row[x] = Color{r, g, b, 1.0f};
            }
        }
        const auto path = (root / name).string();
        ImageWriteOptions options;
        options.format = ImageFormat::Png;
        REQUIRE(save_png(fb, path, options));
        return path;
    }

    fs::path root;
};

DisplayBoundFixture& fixture() {
    static DisplayBoundFixture instance;
    return instance;
}

std::shared_ptr<ImageCache> make_cache() {
    auto cache = std::make_shared<ImageCache>();
    cache->set_backend(std::make_shared<image::StbImageBackend>());
    return cache;
}

double mean_alpha_weighted_r(const CachedImage& image) {
    const std::size_t expected =
        static_cast<std::size_t>(image.width) * image.height * 4;
    REQUIRE(image.gpu_rgba.size() == expected);
    double sum = 0.0;
    for (std::size_t i = 0; i + 3 < image.gpu_rgba.size(); i += 4) {
        sum += image.gpu_rgba[i];
    }
    return sum / (static_cast<double>(image.width) * image.height);
}

} // namespace

TEST_CASE("runtime image decode honours the display bound") {
    const std::string path = fixture().solid_png(
        800, 600, "card.png", 0.25f, 0.5f, 0.75f);
    auto cache = make_cache();

    ImageDecodeOptions bounded;
    bounded.max_dimension = 300;
    const auto image = cache->get_or_load(path, bounded);
    REQUIRE(image != nullptr);
    REQUIRE(image->valid());
    CHECK(image->width == 300);
    CHECK(image->height == 225);
    const std::size_t expected_floats = 300u * 225u * 4u;
    CHECK(image->gpu_rgba.size() == expected_floats);
}

TEST_CASE("bounded decode preserves the premultiplied-linear average") {
    const std::string path = fixture().solid_png(
        800, 600, "average.png", 0.25f, 0.5f, 0.75f);
    auto cache = make_cache();

    ImageDecodeOptions bounded;
    bounded.max_dimension = 128;
    const auto small = cache->get_or_load(path, bounded);
    const auto full = cache->get_or_load(path, ImageDecodeOptions{});
    REQUIRE(small != nullptr);
    REQUIRE(small->valid());
    REQUIRE(full != nullptr);
    REQUIRE(full->valid());
    REQUIRE(full->width == 800);

    const double small_mean = mean_alpha_weighted_r(*small);
    const double full_mean = mean_alpha_weighted_r(*full);
    // Area-average of a solid fill is exact up to 8-bit PNG quantisation of
    // the source; the bound must not shift the average beyond that noise.
    CHECK(std::abs(small_mean - full_mean) < 2e-3);
}

TEST_CASE("display bound never upscales and leaves small images untouched") {
    const std::string path = fixture().solid_png(
        200, 150, "small.png", 0.4f, 0.4f, 0.4f);
    auto cache = make_cache();

    ImageDecodeOptions bounded;
    bounded.max_dimension = 512; // builder floor
    const auto image = cache->get_or_load(path, bounded);
    REQUIRE(image != nullptr);
    REQUIRE(image->valid());
    CHECK(image->width == 200);
    CHECK(image->height == 150);
}

TEST_CASE("different display bounds are different cache entries") {
    const std::string path = fixture().solid_png(
        800, 600, "identity.png", 0.1f, 0.1f, 0.1f);
    auto cache = make_cache();

    ImageDecodeOptions bounded;
    bounded.max_dimension = 300;
    const auto first = cache->canonical_key_for(path, bounded);
    const auto second = cache->canonical_key_for(path, ImageDecodeOptions{});
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first != *second);

    // And the decode bound is part of the identity, not just the key: loading
    // under one bound never returns the other representation.
    const auto small = cache->get_or_load(path, bounded);
    const auto full = cache->get_or_load(path, ImageDecodeOptions{});
    REQUIRE(small != nullptr);
    REQUIRE(full != nullptr);
    CHECK(small->width != full->width);
    // The representation identity used by GPU promotion includes the actual
    // prepared pixels, format and dimensions. Two display bounds must not
    // alias one device-resident surface even though the authored asset is the
    // same file.
    CHECK(small->gpu_key.content_digest != full->gpu_key.content_digest);
    CHECK(small->gpu_key.width == static_cast<std::uint32_t>(small->width));
    CHECK(full->gpu_key.width == static_cast<std::uint32_t>(full->width));
    CHECK(small->gpu_rgba.size() < full->gpu_rgba.size());
    CHECK(cache->get_or_load(path, bounded) == small);
}
