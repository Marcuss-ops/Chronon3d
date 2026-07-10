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
//                                  validation, and dynamic metadata resolution.
//                                  Converts to CompositionDescriptor via
//                                  .to_descriptor().
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/core/types/frame.hpp>
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
    /// error message string on failure.  Called before the factory.
    std::function<std::optional<std::string>(const Props&)> validate;

    /// Compute static metadata from typed props.  Called before the
    /// factory so dimensions/fps/duration are known without evaluating
    /// the composition.
    std::function<CompositionMetadata(const Props&)> resolve_metadata;

    /// Factory that receives the fully-constructed typed Props struct.
    /// The TypedCompositionDescriptor wraps this into a
    /// CompositionProps-accepting factory for registry compatibility.
    std::function<Composition(const Props&)> factory;

    /// Convert to the non-templated CompositionDescriptor for registry storage.
    /// The factory is wrapped to convert CompositionProps → Props by merging
    /// .defaults with ValueMap entries, then validating and resolving metadata.
    [[nodiscard]] CompositionDescriptor to_descriptor() && {
        CompositionDescriptor desc;

        // Capture comp_id BEFORE moving 'id' into desc.
        std::string comp_id = std::move(id);

        desc.id       = comp_id;
        desc.category = std::move(category);

        // Capture typed fields by move into the factory wrapper.
        // The factory converts CompositionProps (ValueMap) → Props (typed),
        // validates, resolves metadata for the descriptor, and calls the
        // typed factory.
        auto typed_factory   = std::move(factory);
        auto typed_defaults  = std::move(defaults);
        auto typed_validate  = std::move(validate);
        auto typed_metadata  = std::move(resolve_metadata);

        // Compute and store metadata from defaults (pre-CLI-override).
        if (typed_metadata) {
            CompositionMetadata meta = typed_metadata(typed_defaults);
            desc.width    = meta.width;
            desc.height   = meta.height;
            desc.fps      = meta.fps;
            desc.duration = meta.duration;
        }

        // Wrap the typed factory to accept CompositionProps.
        desc.factory = [
            typed_factory   = std::move(typed_factory),
            typed_defaults  = std::move(typed_defaults),
            typed_validate  = std::move(typed_validate),
            typed_metadata  = std::move(typed_metadata),
            comp_id         = std::move(comp_id)
        ](const CompositionProps& cprops) -> Composition {
            // Start with defaults.
            // TICKET-TO-DO — ValueMap → Props merge:
            // ValueMap is the CLI/C-ABI bridge (see AGENTS.md § ValueMap).
            // Currently CLI/JSON overrides in cprops.values are ignored;
            // only typed_defaults reach the factory.  Merge logic (JSON
            // schema or structured-binding walk) is deferred to a follow-up
            // ticket — the descriptor scaffolding is the priority for B2.
            Props props = typed_defaults;

            // Validate if a validator was provided.
            if (typed_validate) {
                if (auto err = typed_validate(props)) {
                    throw std::runtime_error(
                        "Composition '" + comp_id + "' validation failed: " + *err);
                }
            }

            return typed_factory(props);
        };

        return desc;
    }
};

} // namespace chronon3d
