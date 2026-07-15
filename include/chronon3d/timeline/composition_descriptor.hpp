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
// ValueMap remains as the CLI/C-ABI bridge for external input. Typed
// descriptors expose a type-erased prepare_props callback so decode,
// validation and dynamic metadata resolution happen before factory creation.
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

using PreparedCompositionMetadata =
    Result<std::optional<CompositionMetadata>, PropsError>;

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

    /// Type-erased props preparation. For typed descriptors this performs
    /// ValueMap decode, typed validation and dynamic metadata resolution
    /// without invoking the composition factory or rendering a frame.
    std::function<PreparedCompositionMetadata(const CompositionProps&)> prepare_props;

    /// Factory function — receives already-prepared CompositionProps at
    /// create() time. Type uses raw std::function to avoid circular include
    /// with composition_registry.hpp. Typed factories still decode the props
    /// to construct their concrete Props value, but validation is owned by
    /// prepare_props / CompositionRegistry::create rather than this wrapper.
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
    /// this before validation and factory creation; defaults are the
    /// bootstrap values supplied to the decoder.
    ///
    /// NOTE: this is the LEGACY decode callback.  New code should prefer
    /// `codec.decode` via PropsCodec, which also carries a declarative
    /// schema.  If both are provided, `codec.decode` takes precedence.
    std::function<chronon3d::Result<Props, PropsError>(
        const ValueMap&, const Props&)> decode;

    /// Factory that receives the fully-decoded typed Props struct.
    /// Validation is performed by the descriptor's prepare_props callback
    /// before CompositionRegistry invokes this factory.
    std::function<Composition(const Props&)> factory;

    /// Optional typed codec carrying schema + decode/encode.  When
    /// provided, `codec.decode` is used in place of the legacy `decode`
    /// callback and `codec.schema` is exported on the resulting
    /// CompositionDescriptor for introspection by CLI/SDK.
    std::optional<PropsCodec<Props>> codec;

    /// Convert to the non-templated CompositionDescriptor for registry storage.
    [[nodiscard]] CompositionDescriptor to_descriptor() && {
        CompositionDescriptor desc;

        std::string comp_id = std::move(id);
        desc.id       = comp_id;
        desc.category = std::move(category);

        auto typed_factory  = std::move(factory);
        auto typed_defaults = std::move(defaults);
        auto typed_validate = std::move(validate);
        auto typed_metadata = std::move(resolve_metadata);
        auto typed_decode   = std::move(decode);
        auto typed_codec    = std::move(codec);

        if (typed_metadata) {
            CompositionMetadata meta = typed_metadata(typed_defaults);
            desc.width    = meta.width;
            desc.height   = meta.height;
            desc.fps      = meta.fps;
            desc.duration = meta.duration;
        }
        if (typed_codec) {
            desc.schema = typed_codec->schema;
        }

        // Shared type-erased decoder used by both prepare_props and factory.
        // It is intentionally pure and owns no registry/cache/global state.
        const auto decode_props = [
            typed_defaults,
            typed_decode,
            typed_codec
        ](const CompositionProps& cprops) -> Result<Props, PropsError> {
            if (typed_codec && typed_codec->decode) {
                return typed_codec->decode(cprops.values, typed_defaults);
            }
            if (typed_decode) {
                return typed_decode(cprops.values, typed_defaults);
            }
            return typed_defaults;
        };

        desc.prepare_props = [
            decode_props,
            typed_validate,
            typed_metadata
        ](const CompositionProps& cprops) -> PreparedCompositionMetadata {
            auto decoded = decode_props(cprops);
            if (!decoded) {
                return std::move(decoded).error();
            }

            Props props = std::move(decoded).value();
            if (typed_validate) {
                if (auto error = typed_validate(props)) {
                    return PropsError{
                        "",
                        PropsErrorReason::InvalidFormat,
                        std::move(*error)
                    };
                }
            }

            if (typed_metadata) {
                return std::optional<CompositionMetadata>{typed_metadata(props)};
            }
            return std::optional<CompositionMetadata>{};
        };

        desc.factory = [
            decode_props,
            typed_factory = std::move(typed_factory),
            comp_id = std::move(comp_id)
        ](const CompositionProps& cprops) -> Composition {
            auto decoded = decode_props(cprops);
            if (!decoded) {
                const PropsError& error = decoded.error();
                throw std::runtime_error(
                    "Composition '" + comp_id +
                    "' decode failed: [" + error.key + "] " + error.message);
            }
            Props props = std::move(decoded).value();
            return typed_factory(props);
        };

        return desc;
    }
};

} // namespace chronon3d
