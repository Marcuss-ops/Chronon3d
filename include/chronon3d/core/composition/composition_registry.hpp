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
 * B2 — CompositionDescriptor is the canonical registration form: each
 * entry carries a factory callable + metadata (width / height / duration /
 * category). The legacy `add(name, factory)` overload is preserved as a
 * 1-line forward to `add(CompositionDescriptor{.id = name, .factory = f})`
 * for backward compatibility with the 200+ pre-B2 call sites; new code
 * MUST use the descriptor form directly.
 *
 * Factory SSoT: descriptors_[id].factory is the single source of truth.
 * The former `factories_` map (formerly duplicate storage) has been
 * eliminated in TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase-1 (2026-07-14).
 *
 * Forward-point: re-introduce a `[[deprecated]]` marker on the legacy
 * overload (with build-flag escape hatch per AGENTS.md §`### 2×-in-one-chore`
 * rule), and REMOVE the legacy overload post V0.1 (subject to
 * ABI-stability ADR per Cat-2 freeze).
 * See docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md.
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
        if (descriptors_.contains(descriptor.id)) {
            throw std::runtime_error("Duplicate composition: " + descriptor.id);
        }
        if (!descriptor.factory) {
            throw std::runtime_error("CompositionDescriptor has null factory: " + descriptor.id);
        }
        descriptors_[std::move(descriptor.id)] = std::move(descriptor);
    }

    /// Legacy registration — preserved for backward compatibility with
    /// the 200+ pre-B2 call sites; internalized as
    /// `add(CompositionDescriptor{.id, .factory})` so the `factories_`
    /// map (formerly duplicate) has been eliminated.
    ///
    /// Forward-point: re-introduce a `[[deprecated]]` marker (with
    /// build-flag escape hatch), and REMOVE the overload post V0.1:
    /// docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md
    void add(std::string name, Factory factory) {
        add(CompositionDescriptor{.id = std::move(name), .factory = std::move(factory)});
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
        auto it = descriptors_.find(name);
        if (it == descriptors_.end()) {
            throw std::runtime_error(std::string("Unknown composition: ") + std::string(name));
        }
        return it->second.factory(props);
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return descriptors_.contains(name);
    }

    [[nodiscard]] std::vector<std::string> available() const {
        std::vector<std::string> ids;
        for (const auto& [_, desc] : descriptors_) {
            ids.push_back(desc.id);
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
        descriptors_.clear();
    }

private:
    // B2 — descriptors stored by id.  Single source of truth for both
    // metadata queries (descriptor_of / descriptors) AND factory storage
    // (create / contains / available).  The former `factories_` map
    // (duplicate storage) was eliminated in
    // TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase-1 (2026-07-14).
    std::map<std::string, CompositionDescriptor, std::less<>> descriptors_;
};

} // namespace chronon3d
