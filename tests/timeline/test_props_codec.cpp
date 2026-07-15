// SPDX-License-Identifier: MIT
//
// tests/timeline/test_props_codec.cpp
//
// Working example + unit tests for PropsCodec<Props> and PropsSchema.
// Demonstrates the canonical pattern for typed composition props:
//   - declare a plain struct (NewsProps)
//   - build a PropsCodec with a declarative schema + decode/encode
//   - register via TypedCompositionDescriptor::codec
//   - read the schema back from CompositionDescriptor
//   - have CLI/JSON overrides flow through codec.decode into the factory.
//
// Pure header-only test (no RenderBackend, no AssetRegistry).
// TIER=UNIT, default link contract.

#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <stdexcept>
#include <string>

using namespace chronon3d;

namespace {

// ── Example typed props for a "Youtube News" composition ─────────────

struct NewsProps {
    std::string title{"Breaking News"};
    std::string format{"landscape"};   // "landscape" or "portrait"
    int         duration_frames{150};
    float       headline_scale{1.0f};
};

// Build the codec once.  This is the pattern authors copy for their own Props.
PropsCodec<NewsProps> make_news_props_codec() {
    PropsCodec<NewsProps> codec;

    codec.schema = PropsSchema{
        .fields = {
            PropField{
                .name         = "title",
                .type         = PropType::String,
                .required     = true,
                .description  = "Headline text shown in the lower third."
            },
            PropField{
                .name         = "format",
                .type         = PropType::Enum,
                .required     = false,
                .description  = "Output aspect ratio.",
                .default_value = "landscape",
                .enum_values  = {"landscape", "portrait"}
            },
            PropField{
                .name         = "duration_frames",
                .type         = PropType::Integer,
                .required     = false,
                .description  = "Composition duration in frames.",
                .default_value = "150"
            },
            PropField{
                .name         = "headline_scale",
                .type         = PropType::Float,
                .required     = false,
                .description  = "Scale factor for the headline.",
                .default_value = "1.0"
            },
        }
    };

    codec.decode = [](const ValueMap& vals, const NewsProps& defs) -> Result<NewsProps, PropsError> {
        NewsProps p = defs;
        try {
            if (vals.contains("title"))            p.title            = vals.get_string("title");
            if (vals.contains("format"))           p.format           = vals.get_string("format");
            if (vals.contains("duration_frames"))  p.duration_frames  = vals.get_int("duration_frames");
            if (vals.contains("headline_scale"))   p.headline_scale   = vals.get_float("headline_scale");
        } catch (const std::exception& e) {
            return PropsError{"", PropsErrorReason::BadType, e.what()};
        }
        return p;
    };

    codec.encode = [](const NewsProps& p) -> ValueMap {
        ValueMap out;
        out.set("title",             p.title);
        out.set("format",            p.format);
        out.set("duration_frames",   std::to_string(p.duration_frames));
        out.set("headline_scale",    std::to_string(p.headline_scale));
        return out;
    };

    return codec;
}

// ── Stub factory that captures the merged props ───────────────────────

struct NewsCaptured {
    std::string title;
    std::string format;
    int         duration_frames{-1};
    float       headline_scale{-1.0f};
    int         call_count{0};
};

Composition make_stub_composition() {
    CompositionSpec spec;
    spec.name       = "news-stub";
    spec.width      = 1;
    spec.height     = 1;
    spec.frame_rate = FrameRate{1, 1};
    spec.duration   = Frame{1};
    return Composition(std::move(spec),
                       [](const FrameContext&) { return Scene{}; });
}

auto make_capturing_factory(NewsCaptured* cap) {
    return [cap](const NewsProps& p) -> Composition {
        cap->title           = p.title;
        cap->format          = p.format;
        cap->duration_frames = p.duration_frames;
        cap->headline_scale  = p.headline_scale;
        ++cap->call_count;
        return make_stub_composition();
    };
}

} // namespace

TEST_CASE("PropsCodec: schema is exported on CompositionDescriptor") {
    CompositionRegistry registry;

    NewsCaptured cap_dummy;
    registry.add(TypedCompositionDescriptor<NewsProps>{
        .id       = "YoutubeNews",
        .category = "News",
        .defaults = NewsProps{},
        .factory  = make_capturing_factory(&cap_dummy),
        .codec    = make_news_props_codec(),
    }.to_descriptor());

    auto desc = registry.descriptor_of("YoutubeNews");
    REQUIRE(desc.has_value());
    REQUIRE(desc->schema.has_value());

    const auto& fields = desc->schema->fields;
    REQUIRE(fields.size() == 4);

    CHECK(fields[0].name        == "title");
    CHECK(fields[0].type         == PropType::String);
    CHECK(fields[0].required    == true);
    CHECK(fields[1].name        == "format");
    CHECK(fields[1].type         == PropType::Enum);
    CHECK(fields[1].enum_values.size() == 2);
    CHECK(fields[2].name        == "duration_frames");
    CHECK(fields[2].type         == PropType::Integer);
    CHECK(fields[3].name        == "headline_scale");
    CHECK(fields[3].type         == PropType::Float);
}

TEST_CASE("PropsCodec: decode merges ValueMap overrides into typed Props") {
    NewsCaptured cap;
    auto desc = TypedCompositionDescriptor<NewsProps>{
        .id       = "YoutubeNews",
        .category = "News",
        .defaults = NewsProps{"Breaking News", "landscape", 150, 1.0f},
        .factory  = make_capturing_factory(&cap),
        .codec    = make_news_props_codec(),
    }.to_descriptor();

    CompositionProps cprops;
    cprops.values.set("title",           "Election Results");
    cprops.values.set("format",          "portrait");
    cprops.values.set("duration_frames", "300");
    cprops.values.set("headline_scale",  "1.5");

    (void)desc.factory(cprops);

    CHECK(cap.title           == "Election Results");
    CHECK(cap.format          == "portrait");
    CHECK(cap.duration_frames == 300);
    CHECK(cap.headline_scale  == 1.5f);
    CHECK(cap.call_count      == 1);
}

TEST_CASE("PropsCodec: encode round-trips defaults") {
    auto codec = make_news_props_codec();

    NewsProps p{"Live Update", "landscape", 180, 1.2f};
    ValueMap  vm = codec.encode(p);

    CHECK(vm.get_string("title")           == "Live Update");
    CHECK(vm.get_string("format")          == "landscape");
    CHECK(vm.get_int("duration_frames")    == 180);
    CHECK(vm.get_float("headline_scale")   == 1.2f);

    // Decode the encoded map back into a fresh Props struct.
    auto decoded = codec.decode(vm, NewsProps{});
    REQUIRE(decoded);
    CHECK(decoded->title           == "Live Update");
    CHECK(decoded->format          == "landscape");
    CHECK(decoded->duration_frames == 180);
    CHECK(decoded->headline_scale  == 1.2f);
}

TEST_CASE("PropsCodec: registry.create round-trip with ValueMap") {
    NewsCaptured cap;
    CompositionRegistry registry;

    registry.add(TypedCompositionDescriptor<NewsProps>{
        .id       = "YoutubeNews",
        .category = "News",
        .defaults = NewsProps{"Breaking News", "landscape", 150, 1.0f},
        .factory  = make_capturing_factory(&cap),
        .codec    = make_news_props_codec(),
    }.to_descriptor());

    CompositionProps cprops;
    cprops.values.set("title", "Market Close");
    cprops.values.set("format", "portrait");

    Composition comp = registry.create("YoutubeNews", cprops);
    (void)comp;

    CHECK(cap.title  == "Market Close");
    CHECK(cap.format == "portrait");
    CHECK(cap.duration_frames == 150);   // default preserved
    CHECK(cap.headline_scale  == 1.0f);  // default preserved
}

TEST_CASE("PropsCodec: codec.decode takes precedence over legacy decode") {
    // If both codec.decode and legacy decode are supplied, the codec path
    // must win and the legacy callback must be ignored.
    struct LocalProps {
        std::string value{"default"};
    };

    PropsCodec<LocalProps> codec;
    codec.schema = PropsSchema{.fields = {PropField{.name = "value", .type = PropType::String}}};
    codec.decode = [](const ValueMap&, const LocalProps&) -> Result<LocalProps, PropsError> {
        return LocalProps{"from-codec"};
    };

    LocalProps captured;
    auto desc = TypedCompositionDescriptor<LocalProps>{
        .id      = "codec-priority",
        .defaults = LocalProps{"default"},
        // Legacy decode would set value to "from-legacy".
        .decode   = [](const ValueMap&, const LocalProps&) -> Result<LocalProps, PropsError> {
            return LocalProps{"from-legacy"};
        },
        .factory  = [&captured](const LocalProps& p) -> Composition {
            captured = p;
            return make_stub_composition();
        },
        .codec    = std::move(codec),
    }.to_descriptor();

    CompositionProps cprops;
    cprops.values.set("value", "cli-value");
    (void)desc.factory(cprops);

    CHECK(captured.value == "from-codec");
}
