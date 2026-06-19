#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

class CompositionRegistry;

namespace detail {

using CompositionFactory = std::function<Composition()>;

struct BuiltinCompositionEntry {
    std::string id;
    CompositionFactory factory;
};

inline std::vector<BuiltinCompositionEntry>& builtin_composition_entries() {
    static std::vector<BuiltinCompositionEntry> entries;
    return entries;
}

inline void add_builtin_composition(std::string_view id, CompositionFactory factory) {
    auto& entries = builtin_composition_entries();
    for (const auto& entry : entries) {
        if (entry.id == id) return;  // idempotent — safe to call multiple times
    }
    entries.push_back({std::string(id), std::move(factory)});
}

inline void populate_registered_compositions(CompositionRegistry& registry);

} // namespace detail

} // namespace chronon3d
