#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

/// Raw input for resolving a composition before factory invocation.
/// Carries the same information as CompositionProps but is intentionally
/// a separate type so callers can build it from CLI/JSON without needing
/// to know the internal AssetRegistry pointer up front.
struct CompositionInput {
    ValueMap values;
    std::filesystem::path project_root;
    AssetRegistry* assets = nullptr;
};

/// Result of resolving a composition against a registry entry.
/// Contains the normalized props (decoded/validated by the descriptor's
/// prepare_props callback) and the resolved metadata, if available.
struct ResolvedCompositionSpec {
    CompositionProps props;
    std::optional<CompositionMetadata> metadata;
};

/**
 * CompositionRegistry holds factories for creating compositions by name.
 * Uses std::map to ensure deterministic (alphabetical) order in available().
 *
 * B2 — CompositionDescriptor is the canonical registration form: each
 * entry carries a factory callable + metadata (width / height / duration /
 * category). The legacy `add(name, factory)` overload has been removed;
 * all registrations now use the descriptor form directly.
 *
 * Factory SSoT: descriptors_[id].factory is the single source of truth.
 * Typed props decode/validation/metadata resolution is exposed by the same
 * descriptor through prepare_props and is run before factory invocation.
 *
 * Starts empty — compositions are added explicitly via add() during
 * startup (ExtensionModule::register_all) or directly by the host.
 */
class CompositionRegistry {
public:
    using Factory = std::function<Composition(const CompositionProps&)>;

    CompositionRegistry() = default;

    void add(CompositionDescriptor descriptor) {
        if (descriptor.id.empty()) {
            throw std::runtime_error("CompositionDescriptor has an empty id");
        }
        if (descriptors_.contains(descriptor.id)) {
            throw std::runtime_error("Duplicate composition: " + descriptor.id);
        }
        if (!descriptor.prepare_props && !descriptor.factory) {
            throw std::runtime_error(
                "CompositionDescriptor has neither prepare_props nor factory: " +
                descriptor.id);
        }

        // Canonicalize untyped registrations at the registry boundary. The
        // resulting descriptor follows the same prepare → construct flow as a
        // typed descriptor, without introducing a second execution path.
        if (!descriptor.prepare_props) {
            const Factory factory = descriptor.factory;
            descriptor.prepare_props = [factory](const CompositionProps& props)
                -> PreparedCompositionResult {
                PreparedComposition prepared;
                prepared.construct = [factory, props]() -> Composition {
                    return factory(props);
                };
                return prepared;
            };
        }

        const std::string key = descriptor.id;
        descriptors_.emplace(key, std::move(descriptor));
    }



    [[nodiscard]] Composition create(std::string_view name) const {
        return create(name, CompositionProps{});
    }

    [[nodiscard]] Composition create(std::string_view name,
                                     const CompositionProps& props) const {
        const auto it = descriptors_.find(name);
        if (it == descriptors_.end()) {
            throw std::runtime_error(
                std::string("Unknown composition: ") + std::string(name));
        }

        const CompositionDescriptor& descriptor = it->second;
        auto prepared_result = descriptor.prepare_props(props);
        if (!prepared_result) {
            const PropsError& error = prepared_result.error();
            const std::string key = error.key.empty()
                ? std::string{}
                : " [" + error.key + "]";
            throw std::runtime_error(
                "Composition '" + descriptor.id +
                "' props failed" + key + ": " + error.message);
        }

        PreparedComposition prepared = std::move(prepared_result).value();
        if (!prepared.construct) {
            throw std::runtime_error(
                "Composition '" + descriptor.id +
                "' preparation returned no constructor");
        }
        return prepared.construct();
    }

    /// Resolve a composition by name from raw input.
    /// Runs the descriptor's prepare_props callback (if present) to decode,
    /// validate and resolve metadata without invoking the factory.
    /// Returns the normalized props and resolved metadata, or a PropsError
    /// if the composition is unknown or props validation fails.
    [[nodiscard]] Result<ResolvedCompositionSpec, PropsError>
    resolve(std::string_view name, const CompositionInput& input) const {
        auto it = descriptors_.find(name);
        if (it == descriptors_.end()) {
            return PropsError{
                "",
                PropsErrorReason::MissingRequired,
                "Unknown composition: " + std::string(name)
            };
        }

        const auto& descriptor = it->second;
        CompositionProps props;
        props.values = input.values;
        props.project_root = input.project_root;
        props.assets = input.assets;

        if (descriptor.prepare_props) {
            auto prepared = descriptor.prepare_props(props);
            if (!prepared) {
                return prepared.error();
            }
            return ResolvedCompositionSpec{
                std::move(props),
                prepared.value().metadata
            };
        }

        std::optional<CompositionMetadata> meta;
        if (descriptor.width && descriptor.height &&
            descriptor.fps && descriptor.duration) {
            meta = CompositionMetadata{
                *descriptor.width,
                *descriptor.height,
                *descriptor.fps,
                *descriptor.duration
            };
        }
        return ResolvedCompositionSpec{std::move(props), meta};
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return descriptors_.contains(name);
    }

    [[nodiscard]] std::vector<std::string> available() const {
        std::vector<std::string> ids;
        ids.reserve(descriptors_.size());
        for (const auto& [_, descriptor] : descriptors_) {
            ids.push_back(descriptor.id);
        }
        return ids;
    }

    [[nodiscard]] std::optional<CompositionDescriptor>
    descriptor_of(std::string_view name) const {
        const auto it = descriptors_.find(name);
        if (it == descriptors_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::vector<CompositionDescriptor> descriptors() const {
        std::vector<CompositionDescriptor> result;
        result.reserve(descriptors_.size());
        for (const auto& [_, descriptor] : descriptors_) {
            result.push_back(descriptor);
        }
        return result;
    }

    void clear() noexcept {
        descriptors_.clear();
    }

private:
    std::map<std::string, CompositionDescriptor, std::less<>> descriptors_;
};

} // namespace chronon3d
