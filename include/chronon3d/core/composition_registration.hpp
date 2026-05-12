#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <vector>

namespace chronon3d {

using CompositionFactory = Composition (*)();

struct CompositionRegistration {
    const char* name;
    CompositionFactory factory;
};

// Returns the global list of registrations. 
// Uses "Meyers' Singleton" pattern for the vector to ensure it's initialized before use.
inline std::vector<CompositionRegistration>& composition_registrations() {
    static std::vector<CompositionRegistration> regs;
    return regs;
}

struct CompositionAutoRegister {
    CompositionAutoRegister(const char* name, CompositionFactory factory) {
        composition_registrations().push_back({name, factory});
    }
};

/**
 * Registers all compositions from the global list into the provided registry.
 */
inline void register_all_compositions(CompositionRegistry& registry) {
    for (const auto& reg : composition_registrations()) {
        if (registry.contains(reg.name)) {
            throw std::runtime_error("Duplicate composition: " + std::string(reg.name));
        }
        registry.add(reg.name, reg.factory);
    }
}

} // namespace chronon3d

// Macro for easy registration.
// Uses __LINE__ to create unique variable names for registration objects.
#define CHRONON_CONCAT_IMPL(a, b) a##b
#define CHRONON_CONCAT(a, b) CHRONON_CONCAT_IMPL(a, b)

#define CHRONON_REGISTER_COMPOSITION(ID, FACTORY) \
    static ::chronon3d::CompositionAutoRegister \
    CHRONON_CONCAT(_chronon_reg_, __LINE__)(ID, FACTORY);
