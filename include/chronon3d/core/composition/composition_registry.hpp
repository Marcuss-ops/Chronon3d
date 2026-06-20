#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace chronon3d {

/**
 * CompositionRegistry holds factories for creating compositions by name.
 * Uses std::map to ensure deterministic (alphabetical) order in available().
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

    void add(std::string name, Factory factory) {
        if (factories_.contains(name)) {
             throw std::runtime_error("Duplicate composition: " + name);
        }
        factories_[std::move(name)] = std::move(factory);
    }

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
        // std::map already keeps keys sorted.
        return ids;
    }

private:
    std::map<std::string, Factory, std::less<>> factories_;
};

} // namespace chronon3d
