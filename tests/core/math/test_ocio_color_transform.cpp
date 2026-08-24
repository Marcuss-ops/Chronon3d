#ifdef CHRONON3D_ENABLE_OCIO

#include <chronon3d/color/ocio_color_transform.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

using chronon3d::color::OcioColorTransform;

TEST_CASE("OpenColorIO rejects a missing configuration with a diagnostic") {
    std::string error;
    const auto transform = OcioColorTransform::from_config(
        "/chronon3d/does-not-exist.ocio", "Raw", "sRGB", &error);
    CHECK_FALSE(transform.has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("OpenColorIO applies a configured RGB transform") {
    const char* config = std::getenv("CHRONON3D_OCIO_TEST_CONFIG");
    if (config == nullptr || *config == '\0') {
        MESSAGE("set CHRONON3D_OCIO_TEST_CONFIG to run the config-backed lane");
        return;
    }

    std::string error;
    const auto transform = OcioColorTransform::from_config(
        config, "Raw", "sRGB", &error);
    REQUIRE(transform.has_value());
    float rgb[3] = {0.25F, 0.5F, 0.75F};
    CHECK(transform->apply_rgb(rgb));
    CHECK(rgb[0] >= 0.0F);
    CHECK(rgb[0] <= 1.0F);
    CHECK(rgb[1] >= 0.0F);
    CHECK(rgb[1] <= 1.0F);
    CHECK(rgb[2] >= 0.0F);
    CHECK(rgb[2] <= 1.0F);
}

#endif // CHRONON3D_ENABLE_OCIO
