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
    builtin_composition_entries().push_back({std::string(id), std::move(factory)});
}

inline void populate_registered_compositions(CompositionRegistry& registry);

} // namespace detail

#define CHRONON_DETAIL_CONCAT_IMPL(a, b) a##b
#define CHRONON_DETAIL_CONCAT(a, b) CHRONON_DETAIL_CONCAT_IMPL(a, b)

#define CHRONON_REGISTER_COMPOSITION(ID, FACTORY)                                      \
    namespace {                                                                         \
        const bool CHRONON_DETAIL_CONCAT(_chronon_registered_composition_, __LINE__) =  \
            []() {                                                                      \
                ::chronon3d::detail::add_builtin_composition(ID, FACTORY);              \
                return true;                                                           \
            }();                                                                        \
    }

} // namespace chronon3d
