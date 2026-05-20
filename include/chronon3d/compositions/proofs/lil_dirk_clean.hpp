#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::compositions {

Scene build_lil_dirk_clean_scene(const FrameContext& ctx);
Composition LilDirkClean();
Composition LilDirkCleanFast();

} // namespace chronon3d::compositions
