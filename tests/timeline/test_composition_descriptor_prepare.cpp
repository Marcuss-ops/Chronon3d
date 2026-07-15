// SPDX-License-Identifier: MIT
//
// Locks the type-erased CompositionDescriptor::prepare_props contract used by
// CLI validate and CompositionRegistry::create. No renderer/backend required.

#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <optional>
#include <string>

using namespace chronon3d;

namespace {

struct PreparedProps {
    int duration_frames{90};
};

Composition stub_composition(const PreparedProps& props) {
    CompositionSpec spec;
    spec.name = "prepared-stub";
    spec.width = 1920;
    spec.height = 1080;
    spec.frame_rate = FrameRate{30, 1};
    spec.duration = Frame{props.duration_frames};
    return Composition(std::move(spec),
                       [](const FrameContext&) { return Scene{}; });
}

TypedCompositionDescriptor<PreparedProps> make_descriptor(int* factory_calls) {
    PropsCodec<PreparedProps> codec;
    codec.schema = PropsSchema{.fields = {
        PropField{
            .name = "duration_frames",
            .type = PropType::Integer,
            .required = false,
            .description = "Composition duration.",
            .default_value = "90"
        }
    }};
    codec.decode = [](const ValueMap& values,
                      const PreparedProps& defaults)
        -> Result<PreparedProps, PropsError> {
        PreparedProps props = defaults;
        try {
            if (values.contains("duration_frames")) {
                props.duration_frames = values.get_int("duration_frames");
            }
        } catch (const std::exception& error) {
            return PropsError{
                "duration_frames",
                PropsErrorReason::BadType,
                error.what()
            };
        }
        return props;
    };

    return TypedCompositionDescriptor<PreparedProps>{
        .id = "PreparedComposition",
        .category = "Tests",
        .defaults = PreparedProps{},
        .validate = [](const PreparedProps& props) -> std::optional<std::string> {
            if (props.duration_frames <= 0) {
                return "duration_frames must be positive";
            }
            return std::nullopt;
        },
        .resolve_metadata = [](const PreparedProps& props) {
            return CompositionMetadata{
                1920,
                1080,
                FrameRate{30, 1},
                Frame{props.duration_frames}
            };
        },
        .factory = [factory_calls](const PreparedProps& props) {
            ++*factory_calls;
            return stub_composition(props);
        },
        .codec = std::move(codec)
    };
}

} // namespace

TEST_CASE("CompositionDescriptor: prepare_props resolves metadata without factory") {
    int factory_calls = 0;
    auto descriptor = make_descriptor(&factory_calls).to_descriptor();

    CompositionProps props;
    props.values.set("duration_frames", "240");

    REQUIRE(descriptor.prepare_props);
    auto prepared = descriptor.prepare_props(props);
    REQUIRE(prepared);
    REQUIRE(prepared->has_value());
    CHECK((*prepared)->width == 1920);
    CHECK((*prepared)->height == 1080);
    CHECK((*prepared)->fps == FrameRate{30, 1});
    CHECK((*prepared)->duration == Frame{240});
    CHECK(factory_calls == 0);
}

TEST_CASE("CompositionDescriptor: prepare_props rejects invalid props before factory") {
    int factory_calls = 0;
    auto descriptor = make_descriptor(&factory_calls).to_descriptor();

    CompositionProps props;
    props.values.set("duration_frames", "0");

    auto prepared = descriptor.prepare_props(props);
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().message == "duration_frames must be positive");
    CHECK(factory_calls == 0);
}

TEST_CASE("CompositionRegistry: invalid typed props never reach factory") {
    int factory_calls = 0;
    CompositionRegistry registry;
    registry.add(make_descriptor(&factory_calls).to_descriptor());

    CompositionProps props;
    props.values.set("duration_frames", "-5");

    CHECK_THROWS(registry.create("PreparedComposition", props));
    CHECK(factory_calls == 0);
}

TEST_CASE("CompositionRegistry: valid typed props reach construction once") {
    int factory_calls = 0;
    CompositionRegistry registry;
    registry.add(make_descriptor(&factory_calls).to_descriptor());

    CompositionProps props;
    props.values.set("duration_frames", "120");

    const Composition composition = registry.create("PreparedComposition", props);
    CHECK(composition.duration() == Frame{120});
    CHECK(factory_calls == 1);
}
