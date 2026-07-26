#include <doctest/doctest.h>

#include <chronon3d/presets/motion_error.hpp>
#include <chronon3d/presets/motion_preset_packs.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

using chronon3d::presets::MotionError;
using chronon3d::presets::MotionErrorCode;
using chronon3d::presets::to_string;

TEST_CASE("motion_error::enum labels are complete") {
    CHECK(std::string(to_string(MotionErrorCode::MotionPresetNotFound)) ==
          "MotionPresetNotFound");
    CHECK(std::string(to_string(MotionErrorCode::UnknownPackId)) ==
          "UnknownPackId");
    static_assert(noexcept(to_string(MotionErrorCode::MotionPresetNotFound)));
}

TEST_CASE("motion_error::typed error keeps code and path") {
    MotionError error(MotionErrorCode::MotionPresetNotFound, "missing-id");
    CHECK(error.code == MotionErrorCode::MotionPresetNotFound);
    CHECK(error.path == "missing-id");
    CHECK(std::string(error.what()).find("MotionPresetNotFound") !=
          std::string::npos);
    CHECK(std::string(error.what()).find("missing-id") != std::string::npos);
}

TEST_CASE("motion_error::typed error remains an exception") {
    bool typed = false;
    bool runtime = false;
    bool standard = false;

    try {
        throw MotionError(MotionErrorCode::MotionPresetNotFound, "typed");
    } catch (const MotionError&) {
        typed = true;
    }
    try {
        throw MotionError(MotionErrorCode::UnknownPackId, "runtime");
    } catch (const std::runtime_error&) {
        runtime = true;
    }
    try {
        throw MotionError(MotionErrorCode::MotionPresetNotFound, "standard");
    } catch (const std::exception&) {
        standard = true;
    }

    CHECK(typed);
    CHECK(runtime);
    CHECK(standard);
}

TEST_CASE("motion_catalog::builtins apply without mutation") {
    chronon3d::LayerBuilder layer("test_layer", chronon3d::Frame{0},
                                   chronon3d::FrameRate{30, 1});
    const auto& catalog = chronon3d::presets::motion_preset_catalog();
    CHECK(catalog.size() > 0);
    CHECK(catalog.contains("slide_in"));
    CHECK_NOTHROW(catalog.apply(layer, "slide_in"));
}

TEST_CASE("motion_catalog::missing preset reports typed error") {
    chronon3d::LayerBuilder layer("test_layer", chronon3d::Frame{0},
                                   chronon3d::FrameRate{30, 1});
    const chronon3d::presets::MotionPresetCatalog catalog{
        std::vector<chronon3d::presets::MotionPresetDescriptor>{}};

    try {
        catalog.apply(layer, "missing-id");
        FAIL("missing preset was expected to throw");
    } catch (const MotionError& error) {
        CHECK(error.code == MotionErrorCode::MotionPresetNotFound);
        CHECK(error.path == "missing-id");
    }
}
