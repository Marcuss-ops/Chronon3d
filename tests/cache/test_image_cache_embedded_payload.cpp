#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("ImageCache decodes embedded PNG bytes under a stable identity") {
    const auto fixture = std::filesystem::path(CHRONON3D_TEST_SOURCE_DIR) /
        "golden/lottie_cli_gate/image_rgba.png";
    std::ifstream input(fixture, std::ios::binary);
    REQUIRE(input.good());
    std::vector<char> raw((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
    REQUIRE(raw.size() > 32U);
    std::vector<std::byte> encoded(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index)
        encoded[index] = static_cast<std::byte>(
            static_cast<unsigned char>(raw[index]));

    chronon3d::ImageCache cache;
    cache.set_backend(std::make_shared<chronon3d::image::StbImageBackend>());
    constexpr auto identity =
        "chronon3d-embedded:fixture-sha256.png";
    const auto loaded = cache.get_or_load_embedded(identity, encoded);
    REQUIRE(loaded);
    REQUIRE(loaded->valid());
    CHECK(loaded->width == 20);
    CHECK(loaded->height == 10);
    CHECK(cache.find(identity) == loaded);

    const std::vector<std::byte> malformed{
        std::byte{'n'}, std::byte{'o'}, std::byte{'t'}, std::byte{'p'},
        std::byte{'n'}, std::byte{'g'}};
    CHECK_FALSE(cache.get_or_load_embedded(
        "chronon3d-embedded:malformed.png", malformed));
    CHECK_FALSE(cache.get_or_load_embedded("not-embedded.png", encoded));
    CHECK_FALSE(cache.get_or_load_embedded(
        "chronon3d-embedded:empty.png", std::span<const std::byte>{}));
}
