#pragma once

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_state.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <string>
#include <functional>
#include <map>

namespace chronon3d::presets::motion {

struct MotionPresetDescriptor {
    MotionPreset preset{MotionPreset::None};
    std::string name;
    
    using ResolverFn = std::function<void(
        const FrameContext& ctx,
        const MotionObject& obj,
        f32 t,
        MotionState& st
    )>;
    
    ResolverFn resolve;
};

class MotionPresetRegistry {
public:
    static MotionPresetRegistry& instance();
    
    MotionPresetRegistry();
    
    void register_preset(MotionPresetDescriptor desc);
    
    [[nodiscard]] bool contains(MotionPreset preset) const;
    [[nodiscard]] const MotionPresetDescriptor& get(MotionPreset preset) const;
    
private:
    std::map<MotionPreset, MotionPresetDescriptor> m_presets;
    void register_builtins();
};

} // namespace chronon3d::presets::motion
