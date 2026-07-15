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

/// Registry and single source of truth for composition descriptors.
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

    /// Transitional registration surface. Remove after the remaining bundled
    /// content callers are migrated to CompositionDescriptor registration.
    [[deprecated("Use add(CompositionDescriptor{...})")]]
    void add(std::string name, Factory factory) {
        add(CompositionDescriptor{
            .id = std::move(name),
            .factory = std::move(factory)
        });
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
