#pragma once

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <algorithm>
#include <filesystem>
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
struct CompositionInput {
    ValueMap values;
    std::filesystem::path project_root;
};

/// Fully resolved composition specification. The descriptor has already decoded
/// and validated props and returned a construction-only closure. Consumers must
/// call `construct` rather than re-entering a compatibility factory path.
struct ResolvedCompositionSpec {
    CompositionProps props;
    std::optional<CompositionMetadata> metadata;
    std::optional<assets::AssetManifest> asset_manifest;
    std::function<Composition()> construct;
};

/**
 * CompositionRegistry holds one canonical descriptor map in deterministic
 * alphabetical order. Registration is explicit and every descriptor is
 * normalized to the same prepare → construct flow at the boundary.
 */
class CompositionRegistry {
public:
    CompositionRegistry() = default;

    void add(CompositionDescriptor descriptor) {
        if (is_frozen()) {
            throw std::runtime_error(
                "CompositionRegistry is frozen; cannot add composition: " +
                descriptor.id);
        }
        if (descriptor.id.empty()) {
            throw std::runtime_error("CompositionDescriptor has an empty id");
        }
        if (descriptors_.contains(descriptor.id)) {
            throw std::runtime_error("Duplicate composition: " + descriptor.id);
        }
        if (!descriptor.prepare_props) {
            throw std::runtime_error(
                "CompositionDescriptor has no prepare_props: " +
                descriptor.id);
        }

        const std::string key = descriptor.id;
        descriptors_.emplace(key, std::move(descriptor));
    }

    /// Freeze the registry. After freeze(), add() will reject any further
    /// registration. This marks the end of the single-threaded startup
    /// phase and the beginning of runtime resolution.
    void freeze() noexcept { frozen_ = true; }

    /// Query whether the registry has been frozen.
    [[nodiscard]] bool is_frozen() const noexcept { return frozen_; }

    [[nodiscard]] Composition create(std::string_view name) const {
        return create(name, CompositionProps{});
    }

    [[nodiscard]] Composition create(std::string_view name,
                                     const CompositionProps& props) const {
        CompositionInput input;
        input.values = props.values;
        input.project_root = props.project_root;

        auto resolved = resolve(name, input);
        if (!resolved) {
            const PropsError& error = resolved.error();
            const std::string key = error.key.empty()
                ? std::string{}
                : " [" + error.key + "]";
            throw std::runtime_error(
                "Composition '" + std::string(name) +
                "' props failed" + key + ": " + error.message);
        }
        if (!resolved->construct) {
            throw std::runtime_error(
                "Composition '" + std::string(name) +
                "' preparation returned no constructor");
        }
        return resolved->construct();
    }

    /// Decode, validate and prepare construction exactly once.
    [[nodiscard]] Result<ResolvedCompositionSpec, PropsError>
    resolve(std::string_view name, const CompositionInput& input) const {
        const auto it = descriptors_.find(name);
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

        auto prepared_result = descriptor.prepare_props(props);
        if (!prepared_result) {
            return prepared_result.error();
        }

        PreparedComposition prepared = std::move(prepared_result).value();
        if (!prepared.construct) {
            return PropsError{
                "",
                PropsErrorReason::InvalidFormat,
                "Composition '" + descriptor.id +
                    "' preparation returned no constructor"
            };
        }

        return ResolvedCompositionSpec{
            .props = std::move(props),
            .metadata = std::move(prepared.metadata),
            .asset_manifest = std::move(prepared.asset_manifest),
            .construct = std::move(prepared.construct),
        };
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
        frozen_ = false;
    }

private:
    std::map<std::string, CompositionDescriptor, std::less<>> descriptors_;
    bool frozen_{false};
};

} // namespace chronon3d
