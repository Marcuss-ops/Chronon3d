// SPDX-License-Identifier: MIT
//
// Locks the type-erased CompositionDescriptor::prepare_props contract used by
// CLI validate and CompositionRegistry::create. No renderer/backend required.

#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

using namespace chronon3d;

namespace {

struct PreparedProps {
    int duration_frames{90};
    std::string image_path{"images/default.png"};
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

TypedCompositionDescriptor<PreparedProps> make_descriptor(
    int* decode_calls,
    int* factory_calls) {
    PropsCodec<PreparedProps> codec;
    codec.schema = PropsSchema{.fields = {
        PropField{
            .name = "duration_frames",
            .type = PropType::Integer,
            .required = false,
            .description = "Composition duration.",
            .default_value = "90"
        },
        PropField{
            .name = "image_path",
            .type = PropType::Path,
            .required = false,
            .description = "Required image.",
            .default_value = "images/default.png"
        }
    }};
    codec.decode = [decode_calls](const ValueMap& values,
                                  const PreparedProps& defaults)
        -> Result<PreparedProps, PropsError> {
        ++*decode_calls;
        PreparedProps props = defaults;
        try {
            if (values.contains("duration_frames")) {
                props.duration_frames = values.require_int("duration_frames");
            }
            if (values.contains("image_path")) {
                props.image_path = values.require_string("image_path");
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
        .resolve_assets = [](const PreparedProps& props) {
            assets::AssetManifest manifest;
            manifest.add_image(props.image_path, "PreparedComposition/image");
            return manifest;
        },
        .resolve_assets_root = [](const PreparedProps&) {
            return std::filesystem::path{"assets"};
        },
        .factory = [factory_calls](const PreparedProps& props) {
            ++*factory_calls;
            return stub_composition(props);
        },
        .codec = std::move(codec)
    };
}

} // namespace

TEST_CASE("CompositionDescriptor: preparation resolves metadata and assets without factory") {
    int decode_calls = 0;
    int factory_calls = 0;
    auto descriptor = make_descriptor(&decode_calls, &factory_calls).to_descriptor();

    CompositionProps props;
    props.values.set("duration_frames", "240");
    props.values.set("image_path", "images/hero.png");

    REQUIRE(descriptor.prepare_props);
    auto result = descriptor.prepare_props(props);
    REQUIRE(result);

    PreparedComposition prepared = std::move(result).value();
    REQUIRE(prepared.metadata.has_value());
    CHECK(prepared.metadata->width == 1920);
    CHECK(prepared.metadata->height == 1080);
    CHECK(prepared.metadata->fps.numerator == 30);
    CHECK(prepared.metadata->fps.denominator == 1);
    CHECK(prepared.metadata->duration == Frame{240});

    REQUIRE(prepared.asset_manifest.has_value());
    REQUIRE(prepared.asset_manifest->size() == 1);
    CHECK(prepared.asset_manifest->assets()[0].path == "images/hero.png");
    REQUIRE(prepared.assets_root.has_value());
    CHECK(*prepared.assets_root == std::filesystem::path{"assets"});

    CHECK(decode_calls == 1);
    CHECK(factory_calls == 0);
    REQUIRE(prepared.construct);

    const Composition composition = prepared.construct();
    CHECK(composition.duration() == Frame{240});
    CHECK(decode_calls == 1);
    CHECK(factory_calls == 1);
}

TEST_CASE("CompositionDescriptor: preparation rejects invalid props before factory") {
    int decode_calls = 0;
    int factory_calls = 0;
    auto descriptor = make_descriptor(&decode_calls, &factory_calls).to_descriptor();

    CompositionProps props;
    props.values.set("duration_frames", "0");

    auto prepared = descriptor.prepare_props(props);
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error().message == "duration_frames must be positive");
    CHECK(decode_calls == 1);
    CHECK(factory_calls == 0);
}

TEST_CASE("CompositionRegistry: valid typed props decode and construct once") {
    int decode_calls = 0;
    int factory_calls = 0;
    CompositionRegistry registry;
    registry.add(make_descriptor(&decode_calls, &factory_calls).to_descriptor());

    CompositionProps props;
    props.values.set("duration_frames", "120");

    const Composition composition = registry.create("PreparedComposition", props);
    CHECK(composition.duration() == Frame{120});
    CHECK(decode_calls == 1);
    CHECK(factory_calls == 1);
}

TEST_CASE("CompositionRegistry: invalid typed props never reach construction") {
    int decode_calls = 0;
    int factory_calls = 0;
    CompositionRegistry registry;
    registry.add(make_descriptor(&decode_calls, &factory_calls).to_descriptor());

    CompositionProps props;
    props.values.set("duration_frames", "-5");

    CHECK_THROWS(registry.create("PreparedComposition", props));
    CHECK(decode_calls == 1);
    CHECK(factory_calls == 0);
}
