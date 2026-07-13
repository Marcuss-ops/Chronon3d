#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace chronon3d {

/**
 * CompositionRegistry holds factories for creating compositions by name.
 * Uses std::map to ensure deterministic (alphabetical) order in available().
 *
 * B2 — supports CompositionDescriptor for metadata-carrying registration.
 * The old `add(name, factory)` is [[deprecated]] in favour of
 * `add(CompositionDescriptor{...})`.
 *
 * Starts empty — compositions are added explicitly via add() during
 * startup (ExtensionModule::register_all) or directly by the host.
 *
 * Factories receive CompositionProps so external programs can pass
 * dynamic data to compositions without modifying Chronon.
 */
class CompositionRegistry {
public:
    using Factory = std::function<Composition(const CompositionProps&)>;

    CompositionRegistry() = default;

    // ── B2 — Canonical descriptor-based registration ──────────────────

    /// Register a composition with full metadata.
    /// The factory and id are required; all other fields are optional.
    void add(CompositionDescriptor descriptor) {
        if (factories_.contains(descriptor.id)) {
            throw std::runtime_error("Duplicate composition: " + descriptor.id);
        }
        if (!descriptor.factory) {
            throw std::runtime_error("CompositionDescriptor has null factory: " + descriptor.id);
        }
        std::string id = descriptor.id;
        descriptors_[id] = std::move(descriptor);
        factories_[id] = descriptors_[id].factory;
    }

    /// Legacy registration — kept on the canonical surface (non-deprecated)
    /// for backward compatibility with the 200+ call sites that pre-date the
    /// B2 CompositionDescriptor migration. The `add(CompositionDescriptor{...})`
    /// form above remains canonical for new code paths and is required when
    /// metadata (width/height/duration/category) needs to be carried on the
    /// descriptor. See docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md
    /// for the forward-point to consolidate sites onto the B2 descriptor form.
    void add(std::string name, Factory factory) {
        if (factories_.contains(name)) {
             throw std::runtime_error("Duplicate composition: " + name);
        }
        factories_[std::move(name)] = std::move(factory);
    }

    // ── Queries ───────────────────────────────────────────────────────

    /// Create a composition with default (empty) props.
    [[nodiscard]] Composition create(std::string_view name) const {
        CompositionProps props;
        return create(name, props);
    }

    /// Create a composition with the given props.
    [[nodiscard]] Composition create(std::string_view name,
                                     const CompositionProps& props) const {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            throw std::runtime_error(std::string("Unknown composition: ") + std::string(name));
        }
        return it->second(props);
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return factories_.contains(name);
    }

    [[nodiscard]] std::vector<std::string> available() const {
        std::vector<std::string> ids;
        for (const auto& [name, _] : factories_) {
            ids.push_back(name);
        }
        return ids;
    }

    // ── B2 — Metadata queries ─────────────────────────────────────────

    /// Returns the descriptor for a registered composition, or nullopt.
    [[nodiscard]] std::optional<CompositionDescriptor> descriptor_of(std::string_view name) const {
        auto it = descriptors_.find(name);
        if (it == descriptors_.end()) return std::nullopt;
        return it->second;
    }

    /// Returns all descriptors, sorted alphabetically by id.
    [[nodiscard]] std::vector<CompositionDescriptor> descriptors() const {
        std::vector<CompositionDescriptor> out;
        for (const auto& [_, desc] : descriptors_) {
            out.push_back(desc);
        }
        return out;
    }

    /// Drop every registered factory — restores the registry to its
    /// freshly-default-constructed state.
    void clear() noexcept {
        factories_.clear();
        descriptors_.clear();
    }

private:
    std::map<std::string, Factory, std::less<>> factories_;
    // B2 — descriptors stored by id for metadata queries.
    // Compositions registered via the old add(name, factory) path have
    // no descriptor entry (descriptor_of() returns nullopt).
    std::map<std::string, CompositionDescriptor, std::less<>> descriptors_;
};

} // namespace chronon3d
