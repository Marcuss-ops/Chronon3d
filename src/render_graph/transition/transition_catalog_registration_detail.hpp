#pragma once

#include <chronon3d/render_graph/transition/transition_catalog.hpp>

namespace chronon3d::graph {
std::unique_ptr<LayerTransitionProgram> make_none(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_crossfade(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_dip_to_black(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_slide(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_zoom(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_wipe_linear(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_smooth_wipe(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_circle_iris(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_flash(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_procedural_remotion(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_remotion(const LayerTransitionSpec& spec);
std::unique_ptr<LayerTransitionProgram> make_fly_under_fade(const LayerTransitionSpec& spec);
} // namespace chronon3d::graph
