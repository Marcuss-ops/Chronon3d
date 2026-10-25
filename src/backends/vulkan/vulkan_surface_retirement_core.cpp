#include "vulkan_surface_authority.hpp"

namespace chronon3d::backends::vulkan::detail {

std::size_t retire_frame_slot_transients(
    SurfaceBindingMap& surface_bindings,
    SurfaceHandleSet& unplanned_surface_handles,
    RecordingSlotMaskMap& recording_slot_masks,
    FrameSlotTransientLists& frame_slot_transient_handles,
    std::size_t slot,
    std::span<const bool> busy,
    SurfaceRetirementCallbacks callbacks,
    std::size_t retained_slot_limit) {
    if (slot >= frame_slot_transient_handles.size()) return 0;
    auto& handles = frame_slot_transient_handles[slot];
    const bool had_records = !handles.empty();
    std::size_t released = 0;
    for (const auto handle : handles) {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) continue;
        const auto mask = recording_slot_masks.find(handle);
        if (mask != recording_slot_masks.end()) {
            mask->second = static_cast<std::uint8_t>(
                mask->second & ~frame_slot_bit(slot));
            if (!recording_slots_retired(mask->second, busy)) continue;
        }
        const auto physical_slot = binding->second;
        surface_bindings.erase(binding);
        unplanned_surface_handles.erase(handle);
        recording_slot_masks.erase(handle);
        callbacks.clear_initialized(callbacks.context, physical_slot);
        ++released;
    }
    handles.clear();
    if (had_records) {
        callbacks.prune_unused_slots(callbacks.context, retained_slot_limit);
    }
    return released;
}

} // namespace chronon3d::backends::vulkan::detail
