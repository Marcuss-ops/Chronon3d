#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_state.hpp>

#include <functional>
#include <string>
#include <vector>

namespace chronon3d::presets::motion {

struct MotionPresetDescriptor {
    MotionPreset preset{MotionPreset::None};
    std::string name;

    using ResolverFn = std::function<void(
        const FrameContext& ctx,
        const MotionObject& obj,
        f32 t,
        MotionState& state
    )>;

    ResolverFn resolve;
};

/// Immutable catalog used by motion-object evaluation.
class MotionPresetCatalog {
public:
    explicit MotionPresetCatalog(std::vector<MotionPresetDescriptor> presets);

    [[nodiscard]] bool contains(MotionPreset preset) const;
    [[nodiscard]] const MotionPresetDescriptor& get(MotionPreset preset) const;

private:
    std::vector<MotionPresetDescriptor> m_presets;
};

namespace detail {

/// Mutable only while the built-in catalog is assembled during startup.
class MotionPresetCatalogBuilder {
public:
    void register_preset(MotionPresetDescriptor descriptor);
    [[nodiscard]] MotionPresetCatalog build() &&;

private:
    std::vector<MotionPresetDescriptor> m_presets;
};

} // namespace detail

[[nodiscard]] const MotionPresetCatalog& motion_preset_catalog();

} // namespace chronon3d::presets::motion

