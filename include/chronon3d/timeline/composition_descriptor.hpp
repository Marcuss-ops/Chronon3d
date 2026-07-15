// ═══════════════════════════════════════════════════════════════════════════
// composition_descriptor.hpp — typed registration metadata for compositions.
//
// B2 — replaces the bare `registry.add("Name", factory)` pattern with a
// descriptor that carries metadata (dimensions, fps, duration, category)
// alongside the factory.  This makes the registry the single source of
// truth for:
//
//   CLI list/info    → available() + descriptor_of()
//   preflight        → width/height/fps known before render
//   render           → duration and frame rate from descriptor
//   video            → output dimensions from descriptor
//   SDK              → discoverable metadata for external consumers
//   documentation    → auto-generated from registry descriptors
//
// ValueMap remains as the CLI/C-ABI bridge for external input; the
// descriptor's factory still receives `CompositionProps` at create() time.
//
// Two tiers:
//   CompositionDescriptor        — non-templated base, storeable in registry
//   TypedCompositionDescriptor<Props> — templated with typed defaults,
//                                  validation, dynamic metadata resolution,
//                                  and OPTIONAL decode() that merges external
//                                  ValueMap overrides into typed Props before
//                                  the factory runs.  Converts to the
//                                  non-templated CompositionDescriptor via
//                                  .to_descriptor().
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/types.hpp>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace chronon3d {

// ── CompositionMetadata — resolved metadata from typed props ──────────────

/// Static metadata resolved from typed composition props.
/// Returned by TypedCompositionDescriptor<Props>::resolve_metadata().
struct CompositionMetadata {
    i32       width{0};
    i32       height{0};
    FrameRate fps{30, 1};
    Frame     duration{0};
};

// ── CompositionDescriptor (non-templated, registry-storeable) ─────────────

struct CompositionDescriptor {
    /// Unique identifier — same as the `name` parameter in the old API.
    std::string id;

    /// Human-readable category for grouping (e.g. "Showcases", "Tests").
    std::string category;

    /// Canvas width in pixels.
    std::optional<i32> width;

    /// Canvas height in pixels.
    std::optional<i32> height;

    /// Frame rate as a rational (numerator/denominator).
    std::optional<FrameRate> fps;

    /// Total frame count.
    std::optional<Frame> duration;

    /// Declarative schema for the composition's typed props.  Populated
    /// when the descriptor is built from a TypedCompositionDescriptor
    /// that provides a PropsCodec.  Empty for legacy / non-typed props.
    std::optional<PropsSchema> schema;

    /// Factory function — receives CompositionProps at create() time.
    /// Type uses raw std::function to avoid circular include with
    /// composition_registry.hpp.
    std::function<Composition(const CompositionProps&)> factory;
};

// ── TypedCompositionDescriptor<Props> ─────────────────────────────────────

/// Templated descriptor with typed defaults, validation, and dynamic
/// metadata resolution.  Converts to the non-templated
/// CompositionDescriptor via `.to_descriptor()` for registry storage.
///
/// Usage:
///   struct NewsIntroProps { std::string title; int duration_frames{150}; };
///
///   registry.add(TypedCompositionDescriptor<NewsIntroProps>{
///       .id = "news-intro",
///       .category = "News",
///       .defaults = { .title = "Breaking News", .duration_frames = 150 },
///       .decode = [](const ValueMap& vals,
///                    const NewsIntroProps& defs) -> Result<NewsIntroProps, PropsError> {
///           NewsIntroProps p = defs;
///           try {
///               if (vals.contains("title"))        p.title            = vals.get_string("title");
///               if (vals.contains("duration"))      p.duration_frames  = vals.get_int("duration");
///           } catch (const std::exception& e) {
///               return PropsError{"duration", PropsErrorReason::BadType, e.what()};
///           }
///           return p;
///       },
///       .validate = [](const NewsIntroProps& p) -> std::optional<std::string> {
///           if (p.title.empty()) return "title is required";
///           return std::nullopt;
///       },
///       .resolve_metadata = [](const NewsIntroProps& p) {
///           return CompositionMetadata{1920, 1080, {30,1}, Frame{p.duration_frames}};
///       },
///       .factory = [](const NewsIntroProps& p) {
///           return composition({.name="news-intro", .width=1920, .height=1080,
///               .duration=Frame{p.duration_frames}},
///               [title = p.title](const FrameContext& ctx) { ... });
///       }
///   }.to_descriptor());
///
template <typename Props>
struct TypedCompositionDescriptor {
    static_assert(std::is_default_constructible_v<Props>,
                  "Props must be default-constructible for .defaults to work");

    /// Unique identifier.
    std::string id;

    /// Human-readable category for grouping.
    std::string category;

    /// Default values for Props fields.  Merged with values from
    /// CompositionProps::values (ValueMap) at create() time — CLI/JSON
    /// overrides take precedence over defaults.
    Props defaults{};

    /// Validate typed props.  Returns std::nullopt on success, or an
    /// error message string on failure.  Called after the decode step
    /// (when one is present) on the merged Props, BEFORE the factory.
    std::function<std::optional<std::string>(const Props&)> validate;

    /// Compute static metadata from typed props.  Called before the
    /// factory so dimensions/fps/duration are known without evaluating
    /// the composition.  Operates on the typed Props, NOT the
    /// CLI/JSON-supplied ValueMap.
    std::function<CompositionMetadata(const Props&)> resolve_metadata;

    /// Decode CLI/JSON overrides (CompositionProps::values) into the
    /// typed `Props` struct.  When present, `to_descriptor()` invokes
    /// this BEFORE `validate()` AND `factory()`; the typed_defaults act
    /// as the bootstrap values the user mutates inside the lambda.
    /// Returns a structed `PropsError` on missing/bad/out-of-range input
    /// (see `composition_props.hpp`).  Forwarded via the canonical
    /// `chronon3d::Result<Props, PropsError>` shape (NOT a custom
    /// Result variant).  When left empty, `to_descriptor()` falls back
    /// to the historical behavior of using typed_defaults only (CLI/JSON
    /// overrides are silently ignored, as the legacy `TICKET-TO-DO`
    /// block documented before this commit implemented the merge).
    ///
    /// NOTE: this is the LEGACY decode callback.  New code should prefer
    /// `codec.decode` via PropsCodec, which also carries a declarative
    /// schema.  If both are provided, `codec.decode` takes precedence.
    std::function<chronon3d::Result<Props, PropsError>(
        const ValueMap&, const Props&)> decode;

    /// Factory that receives the fully-constructed typed Props struct
    /// (after decode + validate have run).  The TypedCompositionDescriptor
    /// wraps this into a CompositionProps-accepting factory for registry
    /// compatibility.
    std::function<Composition(const Props&)> factory;

    /// Optional typed codec carrying schema + decode/encode.  When
    /// provided, `codec.decode` is used in place of the legacy `decode`
    /// callback and `codec.schema` is exported on the resulting
    /// CompositionDescriptor for introspection by CLI/SDK.
    std::optional<PropsCodec<Props>> codec;

    /// Convert to the non-templated CompositionDescriptor for registry storage.
    /// The factory is wrapped to convert CompositionProps → Props by:
    ///   1. Starting with `.defaults` as the bootstrap Props,
    ///   2. Invoking `.decode(cprops.values, defaults)` IF supplied — this is
    ///      the ValueMap → typed Props merge that the legacy TODO block
    ///      deferred; it allows CLI/JSON overrides to flow through,
    ///   3. Calling `.validate(merged_props)` if a validator is present,
    ///   4. Invoking `.factory(merged_props)`.
    /// Resolve_metadata runs on `defaults` (pre-merge) so the registry can
    /// report dimensions/fps/duration BEFORE the factory is invoked.
    [[nodiscard]] CompositionDescriptor to_descriptor() && {
        CompositionDescriptor desc;

        // Capture comp_id BEFORE moving 'id' into desc.
        std::string comp_id = std::move(id);

        desc.id       = comp_id;
        desc.category = std::move(category);

        // Capture typed fields by move into the factory wrapper.
        auto typed_factory   = std::move(factory);
        auto typed_defaults  = std::move(defaults);
        auto typed_validate  = std::move(validate);
        auto typed_metadata  = std::move(resolve_metadata);
        // Implemented 2026-07-14 via TICKET-V2-VALUEMAP-PROPS-MERGE:
        // captures the user-supplied decode callback (may be empty, in
        // which case the wrap falls through to defaults to preserve the
        // historical behavior of pre-decode compositions).
        auto typed_decode    = std::move(decode);
        // New: typed codec (schema + decode/encode).  If present it
        // supersedes the legacy `decode` callback and exports a schema.
        auto typed_codec     = std::move(codec);

        // Compute and store metadata from defaults (pre-CLI-override).
        if (typed_metadata) {
            CompositionMetadata meta = typed_metadata(typed_defaults);
            desc.width    = meta.width;
            desc.height   = meta.height;
            desc.fps      = meta.fps;
            desc.duration = meta.duration;
        }

        // Export the declarative props schema if a codec was supplied.
        if (typed_codec) {
            desc.schema = typed_codec->schema;
        }

        // Wrap the typed factory to accept CompositionProps.
        desc.factory = [
            typed_factory   = std::move(typed_factory),
            typed_defaults  = std::move(typed_defaults),
            typed_validate  = std::move(typed_validate),
            typed_decode    = std::move(typed_decode),
            typed_codec     = std::move(typed_codec),
            comp_id         = std::move(comp_id)
        ](const CompositionProps& cprops) -> Composition {
            // Begin with the bootstrap defaults so the user's decode
            // callback can mutate them in place via the `defs` reference.
            Props props = typed_defaults;

            // 1. Optional decode step (ValueMap → Props merge).  Priority:
            //    - PropsCodec::decode (new, schema-carrying path)
            //    - legacy `decode` callback (TICKET-V2-VALUEMAP-PROPS-MERGE)
            //    When NEITHER is supplied the historical behavior is
            //    preserved: CLI/JSON overrides in cprops.values are silently
            //    ignored and only typed_defaults reach validate/factory.
            auto run_decode = [&](const auto& decoder) {
                auto decoded = decoder(cprops.values, typed_defaults);
                if (!decoded) {
                    const PropsError& err = decoded.error();
                    throw std::runtime_error(
                        "Composition '" + comp_id +
                        "' decode failed: [" + err.key + "] " + err.message);
                }
                props = std::move(decoded).value();
            };

            if (typed_codec && typed_codec->decode) {
                run_decode(typed_codec->decode);
            } else if (typed_decode) {
                run_decode(typed_decode);
            }

            // 2. Validate if a validator was provided.
            if (typed_validate) {
                if (auto err = typed_validate(props)) {
                    throw std::runtime_error(
                        "Composition '" + comp_id + "' validation failed: " + *err);
                }
            }

            // 3. Invoke the typed factory with the merged Props.
            return typed_factory(props);
        };

        return desc;
    }
};

} // namespace chronon3d
