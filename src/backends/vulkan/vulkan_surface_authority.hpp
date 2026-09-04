#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d::backends::vulkan {

// Owns the Vulkan-native surface binding/materialization state. Planned
// bindings are exact: this authority never silently substitutes another slot.
// Dynamic slot selection is confined to the explicitly unplanned path.
template <typename ImageT, typename OwnerT>
class VulkanSurfaceAuthority {
public:
    struct PhysicalSurface {
        ImageT image{};
        runtime::SurfaceDesc desc{};
    };

    std::unordered_map<std::size_t, PhysicalSurface> physical_surfaces{};
    std::unordered_map<runtime::RenderSurfaceHandle, std::size_t> surface_bindings{};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    std::unordered_set<std::size_t> cuda_ready_surfaces{};
    std::unordered_set<std::size_t> cuda_export_ready_surfaces{};
#endif
    std::vector<runtime::RenderSurfaceHandle> deferred_surface_releases{};
    std::unordered_set<runtime::RenderSurfaceHandle> unplanned_surface_handles{};
    std::size_t next_slot{0};
    std::unordered_map<std::size_t, runtime::ResourceAccess> slot_last_access{};

    [[nodiscard]] bool valid(runtime::RenderSurfaceHandle handle) const noexcept {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return false;
        const auto surface = physical_surfaces.find(binding->second);
        return surface != physical_surfaces.end() && surface->second.image.initialized;
    }

    [[nodiscard]] std::size_t physical_count() const noexcept {
        return physical_surfaces.size();
    }

    [[nodiscard]] std::size_t binding_count() const noexcept {
        return surface_bindings.size();
    }

    [[nodiscard]] std::size_t deferred_release_count() const noexcept {
        return deferred_surface_releases.size();
    }

    void clear_access_state() noexcept { slot_last_access.clear(); }

    [[nodiscard]] bool is_job_persistent(
        runtime::RenderSurfaceHandle handle) const noexcept {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return false;
        const auto physical = physical_surfaces.find(binding->second);
        return physical != physical_surfaces.end() &&
               physical->second.desc.lifetime == runtime::LifetimeClass::JobPersistent;
    }

    ImageT& bind(runtime::RenderSurfaceHandle handle,
                 std::size_t slot,
                 const runtime::SurfaceDesc& desc) {
        if (owner_ != nullptr && owner_->plan_preallocated) {
            return bind_planned_exact(handle, slot, desc);
        }
        return bind_unplanned(handle, slot, desc);
    }

    void prune_unused_slots() { prune_unused_slots_impl(); }

    OwnerT* owner_{nullptr};

protected:
    ImageT& bind_planned_exact(runtime::RenderSurfaceHandle handle,
                               std::size_t slot,
                               const runtime::SurfaceDesc& desc) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_surface = physical_surfaces.find(previous->second);
            if (old_surface != physical_surfaces.end() &&
                old_surface->second.image.initialized) {
                throw std::logic_error(
                    "Vulkan planned surface attempted to change physical slot");
            }
            surface_bindings.erase(previous);
        }

        for (const auto& [bound_handle, bound_slot] : surface_bindings) {
            if (bound_handle != handle && bound_slot == slot) {
                const auto occupied = physical_surfaces.find(slot);
                if (occupied != physical_surfaces.end() &&
                    occupied->second.image.initialized) {
                    throw std::logic_error(
                        "Vulkan planned surface physical slot is already occupied");
                }
            }
        }
        return bind_handle_to_slot_impl(handle, slot, desc);
    }

    ImageT& bind_unplanned(runtime::RenderSurfaceHandle handle,
                           std::size_t slot,
                           const runtime::SurfaceDesc& desc) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_slot = previous->second;
            const auto old_it = physical_surfaces.find(old_slot);
            const bool pinned = old_it != physical_surfaces.end() &&
                                old_it->second.image.initialized;
            if (pinned) slot = old_slot;
            else surface_bindings.erase(previous);
        }
        if (physical_surfaces.contains(slot) &&
            physical_surfaces.at(slot).image.initialized) {
            for (const auto& [bound_handle, bound_slot] : surface_bindings) {
                if (bound_handle != handle && bound_slot == slot) {
                    slot = next_slot++;
                    break;
                }
            }
        }
        return bind_handle_to_slot_impl(handle, slot, desc);
    }

    ImageT& bind_handle_to_slot_impl(runtime::RenderSurfaceHandle handle,
                                     std::size_t slot,
                                     const runtime::SurfaceDesc& desc) {
        auto& physical = physical_surfaces[slot];
        if (physical.image.image != VK_NULL_HANDLE && owner_->plan_preallocated) {
            if (physical.image.width != desc.width || physical.image.height != desc.height) {
                throw std::invalid_argument(
                    "Vulkan preallocated surface dimensions mismatch");
            }
        }
        physical.desc = desc;
        surface_bindings[handle] = slot;
        owner_->stats.physical_surfaces_peak = std::max(
            owner_->stats.physical_surfaces_peak,
            static_cast<std::uint64_t>(physical_surfaces.size()));
        return physical.image;
    }

    void prune_unused_slots_impl() {
        for (auto it = physical_surfaces.begin(); it != physical_surfaces.end();) {
            bool in_use = false;
            for (const auto& [handle, slot] : surface_bindings) {
                (void)handle;
                if (slot == it->first) {
                    in_use = true;
                    break;
                }
            }
            if (in_use ||
                it->second.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                ++it;
                continue;
            }
            owner_->destroy_image(it->second.image);
            it = physical_surfaces.erase(it);
        }
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
