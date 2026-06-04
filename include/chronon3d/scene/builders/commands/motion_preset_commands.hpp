#pragma once

namespace chronon3d {

class LayerCommandRegistry;

/// Register all built-in motion preset commands into the given registry.
/// Called by `LayerCommandRegistry::register_builtins()`.
void register_motion_preset_commands(LayerCommandRegistry& registry);

} // namespace chronon3d
