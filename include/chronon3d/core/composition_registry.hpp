#pragma once

#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <functional>
#include <string>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace chronon3d {

/**
 * CompositionRegistry holds factories for creating compositions by name.
 * Uses std::map to ensure deterministic (alphabetical) order in available().
 */
class CompositionRegistry {
public:
    using Factory = detail::CompositionFactory;

    CompositionRegistry() {
        detail::populate_registered_compositions(*this);
    }

    void add(std::string name, Factory factory) {
        if (factories_.contains(name)) {
             throw std::runtime_error("Duplicate composition: " + name);
        }
        factories_[std::move(name)] = std::move(factory);
    }

    [[nodiscard]] Composition create(const std::string& name) const {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            throw std::runtime_error("Unknown composition: " + name);
        }

        return it->second();
    }

    [[nodiscard]] bool contains(const std::string& name) const {
        return factories_.contains(name);
    }

    [[nodiscard]] std::vector<std::string> available() const {
        std::vector<std::string> ids;
        for (const auto& [name, _] : factories_) {
            ids.push_back(name);
        }
        // std::map already keeps keys sorted.
        return ids;
    }

private:
    std::map<std::string, Factory> factories_;
};

namespace detail {

inline void populate_registered_compositions(CompositionRegistry& registry) {
    for (const auto& entry : builtin_composition_entries()) {
        registry.add(entry.id, entry.factory);
    }
}

} // namespace detail

} // namespace chronon3d
