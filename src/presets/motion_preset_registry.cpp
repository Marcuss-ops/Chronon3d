// =============================================================================
// MotionPresetRegistry — thin dispatcher
//
// The actual preset definitions live in per-category source files:
//   - motion_presets_reveal.cpp
//   - motion_presets_3d.cpp
//   - motion_presets_glow.cpp
//   - motion_presets_idle.cpp
//   - motion_presets_impact.cpp
//
// This file owns the registry lifecycle and the static `instance()` accessor
// only.  Adding a new preset means: pick a category file (or create a new
// one) and append one `r.register_preset({...})` call there — no need to
// touch this dispatcher.
// =============================================================================

#include <chronon3d/presets/motion_preset_registry.hpp>

namespace chronon3d::presets::motion {

// Forward declarations of per-category registrars.  Implemented in their
// respective .cpp files; the linker pulls them in via the static library.
void register_reveal_presets(MotionPresetRegistry& r);
void register_3d_presets(MotionPresetRegistry& r);
void register_glow_presets(MotionPresetRegistry& r);
void register_idle_presets(MotionPresetRegistry& r);
void register_impact_presets(MotionPresetRegistry& r);

MotionPresetRegistry& MotionPresetRegistry::instance() {
    static MotionPresetRegistry inst;
    return inst;
}

MotionPresetRegistry::MotionPresetRegistry() {
    register_builtins();
}

void MotionPresetRegistry::register_preset(MotionPresetDescriptor desc) {
    m_presets[desc.preset] = std::move(desc);
}

bool MotionPresetRegistry::contains(MotionPreset preset) const {
    return m_presets.find(preset) != m_presets.end();
}

const MotionPresetDescriptor& MotionPresetRegistry::get(MotionPreset preset) const {
    auto it = m_presets.find(preset);
    if (it != m_presets.end()) {
        return it->second;
    }
    // Defensive fallback: an unrecognised preset is treated as a no-op rather
    // than crashing the frame.  This covers the case where a new enum value
    // was added to MotionPreset but the matching registrar hasn't been wired
    // into the static library yet.
    static const MotionPresetDescriptor fallback{
        MotionPreset::None, "None", [](const FrameContext&, const MotionObject&, f32, MotionState&) {}
    };
    return fallback;
}

void MotionPresetRegistry::register_builtins() {
    register_reveal_presets(*this);
    register_3d_presets(*this);
    register_glow_presets(*this);
    register_idle_presets(*this);
    register_impact_presets(*this);
}

} // namespace chronon3d::presets::motion
