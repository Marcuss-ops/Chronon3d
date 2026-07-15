// SPDX-License-Identifier: MIT
//
// Unit tests for TypedCompositionDescriptor<Props>::decode and the type-erased
// prepare_props phase. Validation and dynamic metadata resolution belong to
// prepare_props / CompositionRegistry::create; the factory wrapper performs
// decode + construction only for backward-compatible direct factory calls.

#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using namespace chronon3d;

namespace {

Composition make_stub_composition(int duration = 1) {
    CompositionSpec spec;
    spec.name = "test-stub";
    spec.width = 1;
    spec.height = 1;
    spec.frame_rate = FrameRate{1, 1};
    spec.duration = Frame{duration};
    return Composition(std::move(spec),
                       [](const FrameContext&) { return Scene{}; });
}

struct FixturesProps {
    std::string title{"default-title"};
    int duration_frames{60};
    std::string font_path{"fonts/Default.ttf"};
};

struct Captured {
    std::string title;
    int duration{-1};
    std::string font;
    int factory_calls{0};
};

auto make_factory(Captured& captured) {
    return [&captured](const FixturesProps& props) -> Composition {
        captured.title = props.title;
        captured.duration = props.duration_frames;
        captured.font = props.font_path;
        ++captured.factory_calls;
        return make_stub_composition(props.duration_frames);
    };
}

auto decode_fixtures() {
    return [](const ValueMap& values,
              const FixturesProps& defaults) -> Result<FixturesProps, PropsError> {
        FixturesProps props = defaults;
        try {
            if (values.contains("title")) {
                props.title = values.get_string("title");
            }
            if (values.contains("duration")) {
                props.duration_frames = values.get_int("duration");
            }
            if (values.contains("font")) {
                props.font_path = values.get_string("font");
            }
        } catch (const std::exception& error) {
            return PropsError{"duration", PropsErrorReason::BadType, error.what()};
        }
        return props;
    };
}

} // namespace

TEST_CASE("TypedCompositionDescriptor: no decoder preserves defaults") {
    Captured captured;
    auto descriptor = TypedCompositionDescriptor<FixturesProps>{
        .id = "no-decode",
        .defaults = FixturesProps{"Breaking News", 150, "fonts/Inter.ttf"},
        .factory = make_factory(captured)
    }.to_descriptor();

    CompositionProps props;
    props.values.set("title", "ignored");
    props.values.set("duration", "999");

    (void)descriptor.factory(props);
    CHECK(captured.title == "Breaking News");
    CHECK(captured.duration == 150);
    CHECK(captured.font == "fonts/Inter.ttf");
    CHECK(captured.factory_calls == 1);
}

TEST_CASE("TypedCompositionDescriptor: direct factory decodes before construction") {
    Captured captured;
    auto descriptor = TypedCompositionDescriptor<FixturesProps>{
        .id = "with-decode",
        .defaults = FixturesProps{"Breaking News", 150, "fonts/Inter.ttf"},
        .decode = decode_fixtures(),
        .factory = make_factory(captured)
    }.to_descriptor();

    CompositionProps props;
    props.values.set("title", "Hello");
    props.values.set("duration", "180");

    (void)descriptor.factory(props);
    CHECK(captured.title == "Hello");
    CHECK(captured.duration == 180);
    CHECK(captured.font == "fonts/Inter.ttf");
    CHECK(captured.factory_calls == 1);
}

TEST_CASE("TypedCompositionDescriptor: decode errors stop direct construction") {
    Captured captured;
    auto descriptor = TypedCompositionDescriptor<FixturesProps>{
        .id = "decode-error",
        .defaults = FixturesProps{},
        .decode = decode_fixtures(),
        .factory = make_factory(captured)
    }.to_descriptor();

    CompositionProps props;
    props.values.set("duration", "NaN");

    CHECK_THROWS_AS(descriptor.factory(props), std::runtime_error);
    CHECK(captured.factory_calls == 0);
}

TEST_CASE("TypedCompositionDescriptor: prepare_props validates merged props without factory") {
    Captured captured;
    int validate_calls = 0;
    auto descriptor = TypedCompositionDescriptor<FixturesProps>{
        .id = "validate-before-factory",
        .defaults = FixturesProps{"default", 100, "fonts/Default.ttf"},
        .validate = [&validate_calls](const FixturesProps& props)
            -> std::optional<std::string> {
            ++validate_calls;
            if (props.duration_frames <= 0) {
                return "duration must be positive";
            }
            return std::nullopt;
        },
        .resolve_metadata = [](const FixturesProps& props) {
            return CompositionMetadata{
                1920,
                1080,
                FrameRate{30, 1},
                Frame{props.duration_frames}
            };
        },
        .decode = decode_fixtures(),
        .factory = make_factory(captured)
    }.to_descriptor();

    CompositionProps valid;
    valid.values.set("duration", "240");
    auto prepared = descriptor.prepare_props(valid);
    REQUIRE(prepared);
    REQUIRE(prepared->has_value());
    CHECK((*prepared)->duration == Frame{240});
    CHECK(validate_calls == 1);
    CHECK(captured.factory_calls == 0);

    CompositionProps invalid;
    invalid.values.set("duration", "-1");
    auto rejected = descriptor.prepare_props(invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().message == "duration must be positive");
    CHECK(validate_calls == 2);
    CHECK(captured.factory_calls == 0);
}

TEST_CASE("CompositionRegistry: preserves descriptor id and prepares before construction") {
    Captured captured;
    CompositionRegistry registry;
    registry.add(TypedCompositionDescriptor<FixturesProps>{
        .id = "registry-roundtrip",
        .category = "Test",
        .defaults = FixturesProps{"default", 60, "fonts/Default.ttf"},
        .validate = [](const FixturesProps& props) -> std::optional<std::string> {
            if (props.duration_frames <= 0) return "duration must be positive";
            return std::nullopt;
        },
        .resolve_metadata = [](const FixturesProps& props) {
            return CompositionMetadata{
                1920,
                1080,
                FrameRate{30, 1},
                Frame{props.duration_frames}
            };
        },
        .decode = decode_fixtures(),
        .factory = make_factory(captured)
    }.to_descriptor());

    auto descriptor = registry.descriptor_of("registry-roundtrip");
    REQUIRE(descriptor.has_value());
    CHECK(descriptor->id == "registry-roundtrip");
    CHECK(descriptor->category == "Test");
    CHECK(descriptor->width == 1920);
    CHECK(descriptor->height == 1080);
    REQUIRE(descriptor->duration.has_value());
    CHECK(descriptor->duration->integral() == 60);

    CompositionProps valid;
    valid.values.set("title", "registry-hello");
    valid.values.set("duration", "240");
    const Composition composition = registry.create("registry-roundtrip", valid);
    CHECK(composition.duration() == Frame{240});
    CHECK(captured.title == "registry-hello");
    CHECK(captured.duration == 240);
    CHECK(captured.factory_calls == 1);

    CompositionProps invalid;
    invalid.values.set("duration", "0");
    CHECK_THROWS(registry.create("registry-roundtrip", invalid));
    CHECK(captured.factory_calls == 1);
}
