// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <optional>
#include <string>
#include <utility>

using namespace chronon3d;

namespace {
struct TestProps {
    std::string title{"default"};
    int duration{60};
};

struct Capture {
    TestProps props;
    int calls{0};
};

Composition stub(const TestProps& props) {
    CompositionSpec spec;
    spec.name = "stub";
    spec.width = 1;
    spec.height = 1;
    spec.frame_rate = FrameRate{1, 1};
    spec.duration = Frame{props.duration};
    return Composition(std::move(spec), [](const FrameContext&) { return Scene{}; });
}

PropsCodec<TestProps> codec() {
    PropsCodec<TestProps> result;
    result.schema = PropsSchema{.fields = {
        PropField{.name = "title", .type = PropType::String},
        PropField{.name = "duration", .type = PropType::Integer}
    }};
    result.decode = [](const ValueMap& values,
                       const TestProps& defaults) -> Result<TestProps, PropsError> {
        TestProps props = defaults;
        if (values.contains("title")) props.title = values.get_string("title");
        if (values.contains("duration")) props.duration = values.get_int("duration");
        return props;
    };
    return result;
}

TypedCompositionDescriptor<TestProps> descriptor(Capture* capture,
                                                   bool with_codec = true) {
    TypedCompositionDescriptor<TestProps> result{
        .id = "Typed",
        .category = "Tests",
        .defaults = TestProps{},
        .validate = [](const TestProps& props) -> std::optional<std::string> {
            if (props.duration <= 0) return "duration must be positive";
            return std::nullopt;
        },
        .resolve_metadata = [](const TestProps& props) {
            return CompositionMetadata{1920, 1080, {30, 1}, Frame{props.duration}};
        },
        .factory = [capture](const TestProps& props) {
            if (capture) {
                capture->props = props;
                ++capture->calls;
            }
            return stub(props);
        }
    };
    if (with_codec) result.codec = codec();
    return result;
}
} // namespace

TEST_CASE("typed descriptor uses defaults without external props") {
    Capture capture;
    CompositionRegistry registry;
    registry.add(descriptor(&capture, false).to_descriptor());

    const Composition composition = registry.create("Typed");
    CHECK(composition.duration() == Frame{60});
    CHECK(capture.props.title == "default");
    CHECK(capture.calls == 1);
}

TEST_CASE("typed descriptor rejects external props without a codec") {
    Capture capture;
    CompositionRegistry registry;
    registry.add(descriptor(&capture, false).to_descriptor());

    CompositionProps props;
    props.values.set("title", "external");
    CHECK_THROWS(registry.create("Typed", props));
    CHECK(capture.calls == 0);
}

TEST_CASE("PropsCodec is the only external decode path") {
    Capture capture;
    CompositionRegistry registry;
    registry.add(descriptor(&capture).to_descriptor());

    CompositionProps props;
    props.values.set("title", "hello");
    props.values.set("duration", "180");
    const Composition composition = registry.create("Typed", props);

    CHECK(composition.duration() == Frame{180});
    CHECK(capture.props.title == "hello");
    CHECK(capture.calls == 1);
}

TEST_CASE("prepare_props validates and resolves metadata before factory") {
    Capture capture;
    auto value = descriptor(&capture).to_descriptor();

    CompositionProps valid;
    valid.values.set("duration", "240");
    auto prepared = value.prepare_props(valid);
    REQUIRE(prepared);
    REQUIRE(prepared->has_value());
    CHECK((*prepared)->duration == Frame{240});
    CHECK(capture.calls == 0);

    CompositionProps invalid;
    invalid.values.set("duration", "0");
    CHECK_FALSE(value.prepare_props(invalid));
    CHECK(capture.calls == 0);
}
