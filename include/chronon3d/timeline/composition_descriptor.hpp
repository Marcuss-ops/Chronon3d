// ═══════════════════════════════════════════════════════════════════════════
// composition_descriptor.hpp — typed registration metadata for compositions.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace chronon3d {

struct CompositionMetadata {
    i32       width{0};
    i32       height{0};
    FrameRate fps{30, 1};
    Frame     duration{0};
};

/// Fully prepared composition input. Props have already been decoded and
/// validated. `construct` captures the typed props so construction never
/// repeats decode or validation.
struct PreparedComposition {
    std::optional<CompositionMetadata> metadata;
    std::optional<assets::AssetManifest> asset_manifest;
    std::function<Composition()> construct;
};

using PreparedCompositionResult = Result<PreparedComposition, PropsError>;

/// Canonical, registry-storeable composition description.
///
/// The descriptor contains only declarative metadata and a `prepare_props`
/// function that decodes/validates props once and returns a
/// `PreparedComposition`.  There is no factory field: construction is always
/// reached through `PreparedComposition::construct`.
struct CompositionDescriptor {
    std::string id;
    std::string category;
    std::optional<i32> width;
    std::optional<i32> height;
    std::optional<FrameRate> fps;
    std::optional<Frame> duration;
    std::optional<PropsSchema> schema;

    /// Decode, validate and resolve all declarative information exactly once.
    std::function<PreparedCompositionResult(const CompositionProps&)> prepare_props;
};

/// Typed composition descriptor.
///
/// PropsCodec is the single decode surface for external ValueMap input. A
/// descriptor without a codec may use its typed defaults, but supplying
/// external values to it is an error rather than a silently ignored override.
template <typename Props>
struct TypedCompositionDescriptor {
    static_assert(std::is_default_constructible_v<Props>,
                  "Props must be default-constructible");
    static_assert(std::is_copy_constructible_v<Props>,
                  "Props must be copy-constructible for prepared construction");

    std::string id;
    std::string category;
    Props defaults{};

    std::function<std::optional<std::string>(const Props&)> validate;
    std::function<CompositionMetadata(const Props&)> resolve_metadata;
    std::function<assets::AssetManifest(const Props&)> resolve_assets;
    std::function<Composition(const Props&)> factory;
    std::optional<PropsCodec<Props>> codec;

    [[nodiscard]] CompositionDescriptor to_descriptor() && {
        CompositionDescriptor descriptor;

        std::string composition_id = std::move(id);
        descriptor.id = composition_id;
        descriptor.category = std::move(category);

        auto typed_defaults = std::move(defaults);
        auto typed_validate = std::move(validate);
        auto typed_metadata = std::move(resolve_metadata);
        auto typed_assets = std::move(resolve_assets);
        auto typed_factory = std::move(factory);
        auto typed_codec = std::move(codec);

        if (!typed_factory) {
            throw std::invalid_argument(
                "TypedCompositionDescriptor has no factory: " + composition_id);
        }

        if (typed_metadata) {
            const CompositionMetadata metadata = typed_metadata(typed_defaults);
            descriptor.width = metadata.width;
            descriptor.height = metadata.height;
            descriptor.fps = metadata.fps;
            descriptor.duration = metadata.duration;
        }
        if (typed_codec) {
            descriptor.schema = typed_codec->schema;
        }

        const auto decode_props = [
            typed_defaults,
            typed_codec
        ](const CompositionProps& composition_props) -> Result<Props, PropsError> {
            if (!typed_codec) {
                if (!composition_props.values.empty()) {
                    return PropsError{
                        "",
                        PropsErrorReason::InvalidFormat,
                        "composition props were supplied but no PropsCodec is declared"
                    };
                }
                return typed_defaults;
            }

            if (!typed_codec->decode) {
                return PropsError{
                    "",
                    PropsErrorReason::InvalidFormat,
                    "PropsCodec.decode is not configured"
                };
            }
            return typed_codec->decode(composition_props.values, typed_defaults);
        };

        const auto construction_factory = typed_factory;
        descriptor.prepare_props = [
            decode_props,
            typed_validate,
            typed_metadata,
            typed_assets,
            construction_factory
        ](const CompositionProps& composition_props)
            -> PreparedCompositionResult {
            auto decoded = decode_props(composition_props);
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

            PreparedComposition prepared;
            if (typed_metadata) {
                prepared.metadata = typed_metadata(props);
            }
            if (typed_assets) {
                prepared.asset_manifest = typed_assets(props);
            }
            prepared.construct = [
                construction_factory,
                props = std::move(props)
            ]() -> Composition {
                return construction_factory(props);
            };
            return prepared;
        };

        return descriptor;
    }
};

namespace detail {

inline auto prepare_from_factory(std::function<Composition()> factory) {
    return [factory = std::move(factory)](
        const CompositionProps&) -> PreparedCompositionResult {
        PreparedComposition prepared;
        prepared.construct = factory;
        return prepared;
    };
}

inline auto prepare_from_factory(
    std::function<Composition(const CompositionProps&)> factory) {
    return [factory = std::move(factory)](
        const CompositionProps& props) -> PreparedCompositionResult {
        PreparedComposition prepared;
        prepared.construct = [factory, props]() { return factory(props); };
        return prepared;
    };
}

} // namespace detail

/// Create a descriptor for a composition whose factory does not need props.
/// The returned descriptor is fully canonical: it stores only `prepare_props`
/// and the construction closure lives in `PreparedComposition::construct`.
inline CompositionDescriptor make_composition_descriptor(
    std::string id,
    std::function<Composition()> factory) {
    CompositionDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.prepare_props = detail::prepare_from_factory(std::move(factory));
    return descriptor;
}

/// Create a descriptor for a composition whose factory receives the raw
/// `CompositionProps`.  Props are captured at prepare time and forwarded to
/// the factory at construction time.
inline CompositionDescriptor make_composition_descriptor(
    std::string id,
    std::function<Composition(const CompositionProps&)> factory) {
    CompositionDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.prepare_props = detail::prepare_from_factory(std::move(factory));
    return descriptor;
}

/// Decorate an existing `CompositionDescriptor` with a no-props construction
/// closure.  All other fields (width, height, fps, duration, schema, ...) are
/// preserved.
inline CompositionDescriptor make_composition_descriptor(
    CompositionDescriptor descriptor,
    std::function<Composition()> factory) {
    descriptor.prepare_props = detail::prepare_from_factory(std::move(factory));
    return descriptor;
}

/// Decorate an existing `CompositionDescriptor` with a construction closure that
/// receives the raw `CompositionProps`.  All other fields are preserved.
inline CompositionDescriptor make_composition_descriptor(
    CompositionDescriptor descriptor,
    std::function<Composition(const CompositionProps&)> factory) {
    descriptor.prepare_props = detail::prepare_from_factory(std::move(factory));
    return descriptor;
}

} // namespace chronon3d
