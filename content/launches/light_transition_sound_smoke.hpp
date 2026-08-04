#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::launches {

/// Minimal 1920x1080, 30 fps, 60-frame scene-A/scene-B Flash composition.
/// The companion render-plan fixture carries the independently muxed SFX cue.
Composition light_transition_sound_smoke();

} // namespace chronon3d::content::launches
