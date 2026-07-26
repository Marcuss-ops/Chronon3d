// =============================================================================
// MotionPresetCatalog — immutable built-in dispatcher.
// =============================================================================

#include <chronon3d/presets/motion_preset_catalog.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace chronon3d::presets::motion {

namespace {

const MotionPresetDescriptor kMissingMotionPresetDescriptor{
    MotionPreset::None,
    "None",
    [](const FrameContext&, const MotionObject&, f32, MotionState&) {}
};

} // namespace

void register_reveal_presets(detail::MotionPresetCatalogBuilder&);
void register_3d_presets(detail::MotionPresetCatalogBuilder&);
void register_glow_presets(detail::MotionPresetCatalogBuilder&);
void register_idle_presets(detail::MotionPresetCatalogBuilder&);
void register_impact_presets(detail::MotionPresetCatalogBuilder&);

MotionPresetCatalog::MotionPresetCatalog(
    std::vector<MotionPresetDescriptor> presets)
    : m_presets(std::move(presets)) {}

void detail::MotionPresetCatalogBuilder::register_preset(
    MotionPresetDescriptor descriptor) {
    const auto duplicate = std::find_if(
        m_presets.begin(), m_presets.end(),
        [&descriptor](const MotionPresetDescriptor& existing) {
            return existing.preset == descriptor.preset;
        });
    if (duplicate != m_presets.end()) {
        throw std::runtime_error("duplicate motion preset");
    }
    m_presets.push_back(std::move(descriptor));
}

MotionPresetCatalog detail::MotionPresetCatalogBuilder::build() && {
    return MotionPresetCatalog(std::move(m_presets));
}

bool MotionPresetCatalog::contains(MotionPreset preset) const {
    return std::find_if(
        m_presets.begin(), m_presets.end(),
        [preset](const MotionPresetDescriptor& descriptor) {
            return descriptor.preset == preset;
        }) != m_presets.end();
}

const MotionPresetDescriptor& MotionPresetCatalog::get(MotionPreset preset) const {
    const auto it = std::find_if(
        m_presets.begin(), m_presets.end(),
        [preset](const MotionPresetDescriptor& descriptor) {
            return descriptor.preset == preset;
        });
    if (it != m_presets.end()) return *it;

    return kMissingMotionPresetDescriptor;
}

const MotionPresetCatalog kBuiltinMotionPresetCatalog = [] {
    detail::MotionPresetCatalogBuilder builder;
    register_reveal_presets(builder);
    register_3d_presets(builder);
    register_glow_presets(builder);
    register_idle_presets(builder);
    register_impact_presets(builder);
    return std::move(builder).build();
}();

const MotionPresetCatalog& motion_preset_catalog() {
    return kBuiltinMotionPresetCatalog;
}

} // namespace chronon3d::presets::motion
