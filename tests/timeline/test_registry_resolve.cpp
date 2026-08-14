// SPDX-License-Identifier: MIT
//
// tests/timeline/test_registry_resolve.cpp
//
// Unit tests for CompositionRegistry::resolve(comp_id, CompositionInput).
// Locks the resolve() contract: returns normalized props + resolved
// metadata without invoking the composition factory.
//
// Pure header-only test (no RenderBackend, no FontEngine, no
// AssetRegistry).  TIER=UNIT, default link contract.

#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <stdexcept>
#include <string>

using namespace chronon3d;

namespace {

struct NewsProps {
    std::string title{"default-title"};
    int         duration_frames{150};
};

Composition make_stub_composition() {
    CompositionSpec spec;
    spec.name        = "test-stub";
    spec.width       = 1;
    spec.height      = 1;
    spec.frame_rate  = FrameRate{1, 1};
    spec.duration    = Frame{1};
    return Composition(std::move(spec),
                       [](const FrameContext&) { return Scene{}; });
}

} // namespace

TEST_CASE("CompositionRegistry::resolve: returns metadata and normalized props") {
    CompositionRegistry registry;

    registry.add(TypedCompositionDescriptor<NewsProps>{
        .id = "news-intro",
        .category = "News",
        .defaults = NewsProps{"Breaking News", 150},
        .resolve_metadata = [](const NewsProps& p) {
            return CompositionMetadata{1920, 1080, FrameRate{30, 1}, Frame{p.duration_frames}};
        },
        .factory = [](const NewsProps&) { return make_stub_composition(); },
        .codec = PropsCodec<NewsProps>{
            .decode = [](const ValueMap& vals, const NewsProps& defs) -> Result<NewsProps, PropsError> {
                NewsProps p = defs;
                if (vals.contains("title"))    p.title            = vals.get_string("title");
                if (vals.contains("duration")) p.duration_frames  = vals.get_int("duration");
                return p;
            },
        }
    }.to_descriptor());

    CompositionInput input;
    input.values.set("title", "Custom Title");
    input.values.set("duration", "300");
    input.project_root = "/tmp/project";

    auto result = registry.resolve("news-intro", input);
    REQUIRE(result.has_value());

    CHECK(result->props.values.get_string("title") == "Custom Title");
    CHECK(result->props.project_root == "/tmp/project");
    REQUIRE(result->metadata.has_value());
    CHECK(result->metadata->width == 1920);
    CHECK(result->metadata->height == 1080);
    CHECK(result->metadata->fps == FrameRate{30, 1});
    CHECK(result->metadata->duration == Frame{300});
}

TEST_CASE("CompositionRegistry::resolve: unknown composition returns PropsError") {
    CompositionRegistry registry;

    CompositionInput input;
    auto result = registry.resolve("missing", input);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().reason == PropsErrorReason::MissingRequired);
}

TEST_CASE("CompositionRegistry::resolve: validation failure propagates PropsError") {
    CompositionRegistry registry;

    registry.add(TypedCompositionDescriptor<NewsProps>{
        .id = "news-invalid",
        .defaults = NewsProps{"Breaking News", 150},
        .validate = [](const NewsProps& p) -> std::optional<std::string> {
            if (p.duration_frames < 0) return std::string{"duration must be >= 0"};
            return std::nullopt;
        },
        .factory = [](const NewsProps&) { return make_stub_composition(); },
        .codec = PropsCodec<NewsProps>{
            .decode = [](const ValueMap& vals, const NewsProps& defs) -> Result<NewsProps, PropsError> {
                NewsProps p = defs;
                if (vals.contains("duration")) p.duration_frames = vals.get_int("duration");
                return p;
            },
        }
    }.to_descriptor());

    CompositionInput input;
    input.values.set("duration", "-1");

    auto result = registry.resolve("news-invalid", input);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().reason == PropsErrorReason::InvalidFormat);
}

TEST_CASE("CompositionRegistry::freeze blocks registration; clear resets freeze") {
    CompositionRegistry registry;

    registry.add(make_composition_descriptor(
        "first",
        [](const CompositionProps&) { return make_stub_composition(); }));

    CHECK_FALSE(registry.is_frozen());
    registry.freeze();
    CHECK(registry.is_frozen());

    CHECK_THROWS_AS(
        registry.add(make_composition_descriptor(
            "second",
            [](const CompositionProps&) { return make_stub_composition(); })),
        std::runtime_error);

    registry.clear();
    CHECK_FALSE(registry.is_frozen());

    CHECK_NOTHROW(registry.add(make_composition_descriptor(
        "second",
        [](const CompositionProps&) { return make_stub_composition(); })));
}

TEST_CASE("CompositionRegistry::typed content accessors filter by category") {
    CompositionRegistry registry;

    const auto add_tagged = [&registry](std::string id, std::string category) {
        registry.add(TypedCompositionDescriptor<NewsProps>{
            .id = std::move(id),
            .category = std::move(category),
            .defaults = NewsProps{"tagged", 60},
            .factory = [](const NewsProps&) { return make_stub_composition(); },
        }.to_descriptor());
    };

    add_tagged("phrase-alpha", std::string{content_category::Phrase});
    add_tagged("phrase-beta", std::string{content_category::Phrase});
    add_tagged("word-alpha", std::string{content_category::ImportantWord});
    add_tagged("image-alpha", std::string{content_category::Image});
    add_tagged("name-alpha", std::string{content_category::NamedText});
    add_tagged("news-unrelated", "News");

    // Each typed accessor surfaces only its canonical content type.
    CHECK(registry.phrases().size() == 2);
    CHECK(registry.important_words().size() == 1);
    CHECK(registry.images().size() == 1);
    CHECK(registry.named_texts().size() == 1);

    // Unrelated categories are not surfaced by any typed accessor.
    CHECK(registry.by_category("News").size() == 1);
    CHECK(registry.phrases().size() == 2);

    // Deterministic alphabetical order by id.
    const auto phrases = registry.phrases();
    REQUIRE(phrases.size() == 2);
    CHECK(phrases[0].id == "phrase-alpha");
    CHECK(phrases[1].id == "phrase-beta");
}
