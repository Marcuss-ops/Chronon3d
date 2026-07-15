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
    std::optional<std::filesystem::path> assets_root;
    std::function<Composition()> construct;
};

using PreparedCompositionResult = Result<PreparedComposition, PropsError>;

/// Canonical, registry-storeable composition description.
struct CompositionDescriptor {
    std::string id;
    std::string category;
    std::optional<i32> width;
    std::optional<i32> height;
    std::optional<FrameRate> fps;
    std::optional<Frame> duration;
    std::optional<PropsSchema> schema;

    /// Decode, validate and resolve all declarative information exactly once.
    /// The registry installs a construction-only fallback for untyped
    /// descriptors that provide only `factory`.
    std::function<PreparedCompositionResult(const CompositionProps&)> prepare_props;

    /// Registration compatibility surface for untyped descriptors and direct
    /// callers. Canonical registry/CLI execution goes through prepare_props and
    /// PreparedComposition::construct.
    std::function<Composition(const CompositionProps&)> factory;
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
    std::function<std::filesystem::path(const Props&)> resolve_assets_root;
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
        auto typed_assets_root = std::move(resolve_assets_root);
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
            typed_assets_root,
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
            if (typed_assets_root) {
                prepared.assets_root = typed_assets_root(props);
            } else if (!composition_props.project_root.empty()) {
                prepared.assets_root = composition_props.project_root;
            }
            prepared.construct = [
                construction_factory,
                props = std::move(props)
            ]() -> Composition {
                return construction_factory(props);
            };
            return prepared;
        };

        // Direct descriptor.factory calls remain source-compatible. Canonical
        // registry and CLI flows use prepare_props + construct and therefore
        // decode only once.
        descriptor.factory = [
            decode_props,
            typed_factory = std::move(typed_factory),
            composition_id = std::move(composition_id)
        ](const CompositionProps& composition_props) -> Composition {
            auto decoded = decode_props(composition_props);
            if (!decoded) {
                const PropsError& error = decoded.error();
                const std::string key = error.key.empty()
                    ? std::string{}
                    : " [" + error.key + "]";
                throw std::runtime_error(
                    "Composition '" + composition_id +
                    "' props decode failed" + key + ": " + error.message);
            }
            return typed_factory(std::move(decoded).value());
        };

        return descriptor;
    }
};

} // namespace chronon3d
