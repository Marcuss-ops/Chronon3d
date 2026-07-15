// ═══════════════════════════════════════════════════════════════════════════
// composition_descriptor.hpp — typed registration metadata for compositions.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>

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

using PreparedCompositionMetadata =
    Result<std::optional<CompositionMetadata>, PropsError>;

/// Canonical, registry-storeable composition description.
///
/// `prepare_props` is always callable. Untyped descriptors use the default
/// no-op preparation; typed descriptors replace it with PropsCodec decode,
/// validation and dynamic metadata resolution.
struct CompositionDescriptor {
    std::string id;
    std::string category;
    std::optional<i32> width;
    std::optional<i32> height;
    std::optional<FrameRate> fps;
    std::optional<Frame> duration;
    std::optional<PropsSchema> schema;

    std::function<PreparedCompositionMetadata(const CompositionProps&)> prepare_props{
        [](const CompositionProps&) -> PreparedCompositionMetadata {
            return std::optional<CompositionMetadata>{};
        }
    };

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

    std::string id;
    std::string category;
    Props defaults{};

    std::function<std::optional<std::string>(const Props&)> validate;
    std::function<CompositionMetadata(const Props&)> resolve_metadata;
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

        descriptor.prepare_props = [
            decode_props,
            typed_validate,
            typed_metadata
        ](const CompositionProps& composition_props)
            -> PreparedCompositionMetadata {
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

            if (typed_metadata) {
                return std::optional<CompositionMetadata>{typed_metadata(props)};
            }
            return std::optional<CompositionMetadata>{};
        };

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
