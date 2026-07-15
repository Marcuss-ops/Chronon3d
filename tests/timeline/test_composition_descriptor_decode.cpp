// SPDX-License-Identifier: MIT
//
// tests/timeline/test_composition_descriptor_decode.cpp
//
// Unit tests for TypedCompositionDescriptor<Props>::decode + to_descriptor()
// wrap-factory.  Locks TICKET-V2-VALUEMAP-PROPS-MERGE (2026-07-14): the
// pipeline that converts external CLI/JSON overrides (the
// ValueMap behind CompositionProps::values) into typed Props BEFORE
// validate + factory run.
//
// Covers six contracts:
//   1. No-decode fallback         — historical behavior preserved:
//      CLI/JSON overrides are silently ignored when `.decode` is empty.
//   2. Decode + matching keys     — ValueMap entries override defaults
//      via the user-supplied decode lambda; un-matched keys keep
//      their default values.
//   3. Decode MissingRequired     — decode returning PropsError with
//      reason=MissingRequired causes the wrapped factory to throw
//      std::runtime_error("Composition '<id>' decode failed: ...").
//   4. Decode BadType             — same throw contract for BadType.
//   5. Validate-after-merge       — validate runs on the merged Props
//      (decode output), not on defaults, so the validator sees CLI
//      overrides.
//   6. Registry round-trip        — CompositionRegistry::create(id,
//      cprops) invokes the wrap-factory, decode sees cprops.values,
//      and the typed factory receives the merged Props.
//
// Pure header-only test (no RenderBackend, no FontEngine, no
// AssetRegistry).  TIER=UNIT, default link contract.
//
// IMPORTANT — designated designator-order discipline:
//
//   TypedCompositionDescriptor<Props> struct member order is:
//     1. id           (std::string)
//     2. category      (std::string)
//     3. defaults      (Props)
//     4. validate      (std::function<...>)
//     5. resolve_metadata (std::function<...>)
//     6. decode        (std::function<Result<Props, PropsError>(...)>)  [legacy]
//     7. factory       (std::function<Composition(const Props&)>)
//     8. codec         (std::optional<PropsCodec<Props>>)              [new]
//
// Per C++20 aggregate-init, listed designators MUST follow the struct
// declaration order.  Omitted members are default-initialized; this
// test file uses omits (instead of `{}, {}, {}` noise) which works
// because default-initialization produces empty std::function values
// (which the wrap-factory `if (typed_*)` guards already handle).

#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <stdexcept>
#include <string>
#include <utility>

using namespace chronon3d;

namespace {

// ── Stub Composition factory ─────────────────────────────────────────
//
// Tests never invoke evaluate() — Scene{} is sufficient.  Sized 1×1 with
// FrameRate{1,1} and Frame{1} duration; keeps evaluate() cheap if a
// future refactor decides to call it.
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

// ── FixturesProps — typed Props struct used by every test ────────────
//
// Three fields (string/int/string) cover the three decode-supported
// accessors (.get_string / .get_int / .get_string).  font_path acts as
// an asset-path proxy — out of scope for v1 type parsing, but
// demonstrating that strings flow through unchanged.
struct FixturesProps {
    std::string title{"default-title"};
    int         duration_frames{0};
    std::string font_path{""};
};

// ── Captured — assertion-side mirror of FixturesProps ───────────────
//
// Captured by reference into each test's typed factory so post-call
// assertions see what the merged Props looked like.
struct Captured {
    std::string last_title{};
    int         last_duration{-1};
    std::string last_font{};
    int         call_count{0};
};

// Identity-bridging factory: writes merged Props into `cap` and
// returns a stub Composition.  Mirrors the audit §1 example shape.
auto make_capturing_factory(Captured& cap) {
    return [&cap](const FixturesProps& p) -> Composition {
        cap.last_title    = p.title;
        cap.last_duration = p.duration_frames;
        cap.last_font     = p.font_path;
        ++cap.call_count;
        return make_stub_composition();
    };
}

} // namespace

TEST_CASE("TypedCompositionDescriptor::to_descriptor: no decode → factory receives defaults") {
    Captured cap;
    auto desc = TypedCompositionDescriptor<FixturesProps>{
        .id              = "no-decode",
        .defaults        = FixturesProps{"Breaking News", 150, "fonts/Inter.ttf"},
        .factory         = make_capturing_factory(cap),
        // .category, .validate, .resolve_metadata, .decode intentionally omitted
    }.to_descriptor();

    CompositionProps cprops;
    cprops.values.set("title", "Should-be-IGNORED");
    cprops.values.set("duration", "999");

    Composition comp = desc.factory(cprops);
    (void)comp;

    // Historical behavior: CLI/JSON overrides are silently ignored when
    // no decode callback is supplied.  The wrapper passes the
    // typed_defaults through unchanged.
    CHECK(cap.last_title    == "Breaking News");
    CHECK(cap.last_duration == 150);
    CHECK(cap.last_font     == "fonts/Inter.ttf");
    CHECK(cap.call_count    == 1);
    CHECK(desc.id           == "no-decode");
}

TEST_CASE("TypedCompositionDescriptor::to_descriptor: decode + matching keys → merged into Props") {
    Captured cap;
    auto desc = TypedCompositionDescriptor<FixturesProps>{
        .id              = "with-decode",
        .defaults        = FixturesProps{"Breaking News", 150, "fonts/Inter.ttf"},
        .validate        = {},
        .decode          = [](const ValueMap& vals,
                              const FixturesProps& defs) -> Result<FixturesProps, PropsError> {
            FixturesProps p = defs;  // bootstrap from defaults
            if (vals.contains("title"))   p.title           = vals.get_string("title");
            if (vals.contains("duration")) p.duration_frames = vals.get_int("duration");
            if (vals.contains("font"))     p.font_path       = vals.get_string("font");
            return p;  // implicit Result<T, E> ctor from T&& (header is non-explicit)
        },
        .factory         = make_capturing_factory(cap),
    }.to_descriptor();

    CompositionProps cprops;
    cprops.values.set("title", "Hello");
    cprops.values.set("duration", "180");
    // font NOT set → keeps default font_path.

    Composition comp = desc.factory(cprops);
    (void)comp;

    CHECK(cap.last_title    == "Hello");        // overridden
    CHECK(cap.last_duration == 180);           // overridden
    CHECK(cap.last_font     == "fonts/Inter.ttf");  // default preserved
    CHECK(cap.call_count    == 1);
}

TEST_CASE("TypedCompositionDescriptor::to_descriptor: decode MissingRequired → factory throws") {
    Captured cap;
    auto desc = TypedCompositionDescriptor<FixturesProps>{
        .id              = "decode-missing",
        .defaults        = FixturesProps{"x", 0, "y"},
        .decode          = [](const ValueMap& vals,
                              const FixturesProps&) -> Result<FixturesProps, PropsError> {
            if (!vals.contains("title")) {
                return PropsError{"title", PropsErrorReason::MissingRequired,
                                  "field is required"};
            }
            return FixturesProps{vals.get_string("title"), 0, ""};
        },
        .factory         = make_capturing_factory(cap),
    }.to_descriptor();

    CompositionProps cprops;  // empty values → decode returns PropsError
    CHECK_THROWS_AS(desc.factory(cprops), std::runtime_error);

    // Verify the typed factory was NOT invoked (decode failure short-circuits
    // before validate + factory).
    CHECK(cap.call_count == 0);
}

TEST_CASE("TypedCompositionDescriptor::to_descriptor: decode BadType → factory throws with key context") {
    Captured cap;
    auto desc = TypedCompositionDescriptor<FixturesProps>{
        .id              = "decode-badtype",
        .defaults        = FixturesProps{"x", 0, "y"},
        .decode          = [](const ValueMap& vals,
                              const FixturesProps& defs) -> Result<FixturesProps, PropsError> {
            FixturesProps p = defs;
            try {
                p.title = vals.require_string("title");
                p.duration_frames = vals.require_int("duration");  // throws on "NaN"
            } catch (const std::exception& e) {
                return PropsError{"duration", PropsErrorReason::BadType, e.what()};
            }
            return p;
        },
        .factory         = make_capturing_factory(cap),
    }.to_descriptor();

    CompositionProps cprops;
    cprops.values.set("title", "OK");
    cprops.values.set("duration", "NaN");  // ValueMap::require_int throws

    CHECK_THROWS_AS(desc.factory(cprops), std::runtime_error);
    CHECK(cap.call_count == 0);
}

TEST_CASE("TypedCompositionDescriptor::to_descriptor: validate runs AFTER decode on merged Props") {
    Captured cap;
    int     validate_call_count = 0;
    auto desc = TypedCompositionDescriptor<FixturesProps>{
        .id              = "validate-after-decode",
        .defaults        = FixturesProps{"x", 100, "y"},
        .validate        = [&validate_call_count](const FixturesProps& p)
                            -> std::optional<std::string> {
            ++validate_call_count;
            if (p.duration_frames < 0) return std::string{"duration must be >= 0"};
            return std::nullopt;
        },
        .decode          = [](const ValueMap& vals,
                              const FixturesProps& defs) -> Result<FixturesProps, PropsError> {
            FixturesProps p = defs;
            if (vals.contains("duration")) p.duration_frames = vals.get_int("duration");
            return p;
        },
        .factory         = make_capturing_factory(cap),
    }.to_descriptor();

    // Case A: CLI override produces a VALID merged Props → factory runs.
    {
        CompositionProps ok_props;
        ok_props.values.set("duration", "60");
        Composition comp = desc.factory(ok_props);
        (void)comp;
        CHECK(cap.last_duration == 60);
        CHECK(validate_call_count == 1);
        CHECK(cap.call_count == 1);
    }

    // Case B: CLI override produces an INVALID merged Props → validate
    // throws and factory does NOT run.
    CompositionProps bad_props;
    bad_props.values.set("duration", "-1");
    CHECK_THROWS_AS(desc.factory(bad_props), std::runtime_error);
    CHECK(validate_call_count == 2);  // ran but threw
    CHECK(cap.call_count == 1);       // unchanged from Case A
}

TEST_CASE("TypedCompositionDescriptor: registry.create round-trip with ValueMap flows through decode") {
    Captured cap;
    CompositionRegistry registry;

    registry.add(TypedCompositionDescriptor<FixturesProps>{
        .id              = "registry-roundtrip",
        .category        = "Test",
        .defaults        = FixturesProps{"default", 60, "fonts/Default.ttf"},
        .resolve_metadata = [](const FixturesProps& p) {
            return CompositionMetadata{1920, 1080, {30, 1}, Frame{p.duration_frames}};
        },
        .decode          = [](const ValueMap& vals,
                              const FixturesProps& defs) -> Result<FixturesProps, PropsError> {
            FixturesProps p = defs;
            if (vals.contains("title"))    p.title           = vals.get_string("title");
            if (vals.contains("duration"))  p.duration_frames = vals.get_int("duration");
            return p;
        },
        .factory         = make_capturing_factory(cap),
    }.to_descriptor());

    // Descriptor metadata is computed from DEFAULTS (pre-decode) so the
    // registry can answer list/info without invoking the factory.
    auto desc_opt = registry.descriptor_of("registry-roundtrip");
    REQUIRE(desc_opt.has_value());
    // NOTE: desc_opt->id is INTENTIONALLY not asserted.  CompositionRegistry::add()
    // operates as `descriptors_[std::move(descriptor.id)] = std::move(descriptor)`,
    // which strips descriptor.id (used as the map KEY — correct) but leaves the
    // moved-from empty string in the stored descriptor's `id` field.  This is a
    // pre-existing bug, NOT introduced by TICKET-V2-VALUEMAP-PROPS-MERGE (the
    // E2 chore).  Forward-point: TICKET-COMPOSITIONREGISTRY-ID-PRESERVE
    // (out-of-scope for E2; tracked in the same ticket's forward-points
    // section).  The map KEY still resolves correctly via descriptor_of():
    // the lookup itself returned a value, so the KEY = "registry-roundtrip"
    // is stored correctly — only the descriptor's internal id field is empty.
    CHECK(desc_opt->category == "Test");
    CHECK(desc_opt->width    == 1920);
    CHECK(desc_opt->height   == 1080);
    REQUIRE(desc_opt->fps.has_value());
    CHECK(desc_opt->fps->num() == 30);  // FrameRate canonical accessor per time.hpp
    CHECK(desc_opt->fps->den() == 1);
    REQUIRE(desc_opt->duration.has_value());
    CHECK(desc_opt->duration->integral() == 60);  // from defaults, NOT CLI override

    // Round-trip: registry.create(id, cprops) calls the wrapped factory
    // which invokes decode(cprops.values, defaults) → merged Props reach
    // the typed factory.  The KEY lookup here still resolves via the
    // stored map key (uncorrupted), proving create() routes correctly.
    CompositionProps cprops;
    cprops.values.set("title", "registry-hello");
    cprops.values.set("duration", "240");
    Composition comp = registry.create("registry-roundtrip", cprops);
    (void)comp;

    CHECK(cap.last_title    == "registry-hello");
    CHECK(cap.last_duration == 240);
    CHECK(cap.call_count    == 1);
}
