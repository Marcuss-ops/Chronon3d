// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <string>
#include <utility>

using namespace chronon3d;

namespace {
struct NewsProps {
    std::string title{"Breaking News"};
    std::string format{"landscape"};
    int duration_frames{150};
    float headline_scale{1.0f};
};

struct NewsCaptured {
    NewsProps props;
    int call_count{0};
};

Composition stub() {
    CompositionSpec spec;
    spec.name = "news-stub";
    spec.width = 1;
    spec.height = 1;
    spec.frame_rate = FrameRate{1, 1};
    spec.duration = Frame{1};
    return Composition(std::move(spec), [](const FrameContext&) { return Scene{}; });
}

PropsCodec<NewsProps> make_codec() {
    PropsCodec<NewsProps> codec;
    codec.schema = PropsSchema{.fields = {
        PropField{
            .name = "title",
            .type = PropType::String,
            .required = true,
            .description = "Headline text."
        },
        PropField{
            .name = "format",
            .type = PropType::Enum,
            .description = "Output aspect ratio.",
            .default_value = "landscape",
            .enum_values = {"landscape", "portrait"}
        },
        PropField{
            .name = "duration_frames",
            .type = PropType::Integer,
            .description = "Composition duration.",
            .default_value = "150"
        },
        PropField{
            .name = "headline_scale",
            .type = PropType::Float,
            .description = "Headline scale.",
            .default_value = "1.0"
        }
    }};
    codec.decode = [](const ValueMap& values,
                      const NewsProps& defaults) -> Result<NewsProps, PropsError> {
        NewsProps props = defaults;
        if (values.contains("title")) props.title = values.get_string("title");
        if (values.contains("format")) props.format = values.get_string("format");
        if (values.contains("duration_frames")) {
            props.duration_frames = values.get_int("duration_frames");
        }
        if (values.contains("headline_scale")) {
            props.headline_scale = values.get_float("headline_scale");
        }
        return props;
    };
    codec.encode = [](const NewsProps& props) {
        ValueMap values;
        values.set("title", props.title);
        values.set("format", props.format);
        values.set("duration_frames", std::to_string(props.duration_frames));
        values.set("headline_scale", std::to_string(props.headline_scale));
        return values;
    };
    return codec;
}

TypedCompositionDescriptor<NewsProps> make_descriptor(NewsCaptured* captured) {
    return TypedCompositionDescriptor<NewsProps>{
        .id = "YoutubeNews",
        .category = "News",
        .defaults = NewsProps{},
        .factory = [captured](const NewsProps& props) {
            if (captured) {
                captured->props = props;
                ++captured->call_count;
            }
            return stub();
        },
        .codec = make_codec()
    };
}
} // namespace

TEST_CASE("PropsCodec exports schema on CompositionDescriptor") {
    auto descriptor = make_descriptor(nullptr).to_descriptor();
    REQUIRE(descriptor.schema.has_value());
    REQUIRE(descriptor.schema->fields.size() == 4);
    CHECK(descriptor.schema->fields[0].name == "title");
    CHECK(descriptor.schema->fields[0].required);
    CHECK(descriptor.schema->fields[1].enum_values.size() == 2);
}

TEST_CASE("PropsCodec merges ValueMap overrides into typed props") {
    NewsCaptured captured;
    CompositionRegistry registry;
    registry.add(make_descriptor(&captured).to_descriptor());

    CompositionProps props;
    props.values.set("title", "Election Results");
    props.values.set("format", "portrait");
    props.values.set("duration_frames", "300");
    props.values.set("headline_scale", "1.5");
    (void)registry.create("YoutubeNews", props);

    CHECK(captured.props.title == "Election Results");
    CHECK(captured.props.format == "portrait");
    CHECK(captured.props.duration_frames == 300);
    CHECK(captured.props.headline_scale == 1.5f);
    CHECK(captured.call_count == 1);
}

TEST_CASE("PropsCodec encode and decode round-trip") {
    auto codec = make_codec();
    const NewsProps input{"Live Update", "portrait", 180, 1.2f};
    const ValueMap values = codec.encode(input);
    auto decoded = codec.decode(values, NewsProps{});

    REQUIRE(decoded);
    CHECK(decoded->title == input.title);
    CHECK(decoded->format == input.format);
    CHECK(decoded->duration_frames == input.duration_frames);
    CHECK(decoded->headline_scale == input.headline_scale);
}

TEST_CASE("PropsCodec preserves defaults for omitted values") {
    NewsCaptured captured;
    CompositionRegistry registry;
    registry.add(make_descriptor(&captured).to_descriptor());

    CompositionProps props;
    props.values.set("title", "Market Close");
    (void)registry.create("YoutubeNews", props);

    CHECK(captured.props.title == "Market Close");
    CHECK(captured.props.format == "landscape");
    CHECK(captured.props.duration_frames == 150);
    CHECK(captured.props.headline_scale == 1.0f);
}
