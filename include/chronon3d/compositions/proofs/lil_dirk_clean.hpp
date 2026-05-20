#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::compositions {

void build_lil_dirk_clean_background(SceneBuilder& s, const FrameContext& ctx);
Scene build_lil_dirk_clean_scene(const FrameContext& ctx);
Composition LilDirkClean();
Composition LilDirkCleanFast();

} // namespace chronon3d::compositions
