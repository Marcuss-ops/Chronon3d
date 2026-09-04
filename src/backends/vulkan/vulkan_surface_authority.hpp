#pragma once

#ifdef CHRONON3D_ENABLE_VULKAN

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d::backends::vulkan {

// Sole authority for Vulkan-native surface binding/materialization state and
// physical-slot selection. Planned bindings are exact: the authority never
// silently substitutes another slot. Dynamic slot selection is confined to
// the explicitly unplanned/lazy path.
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

    OwnerT* owner_{nullptr};

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

    [[nodiscard]] bool slot_in_use(std::size_t slot) const noexcept {
        for (const auto& [handle, bound_slot] : surface_bindings) {
            (void)handle;
            if (bound_slot == slot) return true;
        }
        return false;
    }

    [[nodiscard]] std::size_t bound_slot(runtime::RenderSurfaceHandle handle) const {
        const auto it = surface_bindings.find(handle);
        if (it == surface_bindings.end()) {
            throw std::invalid_argument(
                "Vulkan surface handle " + std::to_string(handle) +
                " is not bound to a physical slot (bindings=" +
                std::to_string(surface_bindings.size()) +
                ", physical_surfaces=" +
                std::to_string(physical_surfaces.size()) + ")");
        }
        return it->second;
    }

    ImageT& resolve(runtime::RenderSurfaceHandle handle) {
        const auto slot = bound_slot(handle);
        const auto physical = physical_surfaces.find(slot);
        if (physical == physical_surfaces.end()) {
            throw std::invalid_argument("Vulkan physical slot has no backing image");
        }
        return physical->second.image;
    }

    [[nodiscard]] bool slot_has_initialized_occupant(
        std::size_t slot, runtime::RenderSurfaceHandle self) const noexcept {
        const auto physical = physical_surfaces.find(slot);
        if (physical == physical_surfaces.end() ||
            !physical->second.image.initialized) {
            return false;
        }
        for (const auto& [handle, bound_slot] : surface_bindings) {
            if (handle != self && bound_slot == slot) return true;
        }
        return false;
    }

    ImageT& bind(runtime::RenderSurfaceHandle handle,
                 std::size_t slot,
                 const runtime::SurfaceDesc& desc) {
        require_owner();
        if (owner_->plan_preallocated) {
            return bind_planned_exact(handle, slot, desc);
        }
        return bind_unplanned(handle, slot, desc);
    }

    ImageT& ensure(runtime::RenderSurfaceHandle handle,
                   const runtime::SurfaceDesc& desc) {
        require_owner();
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            (desc.format != runtime::PixelFormat::Rgba32Float &&
             desc.format != runtime::PixelFormat::Rgba8Unorm &&
             desc.format != runtime::PixelFormat::R8Unorm &&
             desc.format != runtime::PixelFormat::Nv12 &&
             desc.format != runtime::PixelFormat::P010) ||
            desc.width == 0 || desc.height == 0) {
            throw std::invalid_argument(
                "Vulkan surface requires a valid non-empty description");
        }

        const auto binding = surface_bindings.find(handle);
        if (binding != surface_bindings.end()) {
            auto& physical = physical_surfaces.at(binding->second);
            if (physical.image.image == VK_NULL_HANDLE ||
                physical.image.width != desc.width ||
                physical.image.height != desc.height ||
                physical.desc.format != desc.format) {
                if (physical.image.image != VK_NULL_HANDLE) {
                    owner_->destroy_image(physical.image);
                }
                owner_->make_image(physical.image, desc.width, desc.height, false,
                                   owner_->to_vk_format(desc.format));
                physical.desc = desc;
            }
            physical.image.text_atlas_encoding = desc.text_atlas_encoding;
            owner_->ensure_descriptor_set();
            return physical.image;
        }

        // Persistent assets never alias transient slots. For transients, prefer
        // an exact compatible unused slot, then any unused transient slot.
        if (desc.lifetime != runtime::LifetimeClass::JobPersistent) {
            for (auto& [slot, physical] : physical_surfaces) {
                if (!slot_in_use(slot) &&
                    owner_->surface_compatible(physical.desc, desc)) {
                    owner_->ensure_descriptor_set();
                    return bind(handle, slot, desc);
                }
            }
            for (auto& [slot, physical] : physical_surfaces) {
                if (!slot_in_use(slot) &&
                    physical.desc.lifetime != runtime::LifetimeClass::JobPersistent) {
                    owner_->ensure_descriptor_set();
                    return bind(handle, slot, desc);
                }
            }
        }

        owner_->ensure_descriptor_set();
        return bind(handle, next_slot++, desc);
    }

    void prune_unused_slots() {
        require_owner();
        for (auto it = physical_surfaces.begin(); it != physical_surfaces.end();) {
            if (slot_in_use(it->first) ||
                it->second.desc.lifetime == runtime::LifetimeClass::JobPersistent) {
                ++it;
                continue;
            }
            owner_->destroy_image(it->second.image);
            it = physical_surfaces.erase(it);
        }
    }

protected:
    void require_owner() const {
        if (owner_ == nullptr) {
            throw std::logic_error("Vulkan surface authority has no owner");
        }
    }

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

        if (slot_has_initialized_occupant(slot, handle)) {
            throw std::logic_error(
                "Vulkan planned surface physical slot is already occupied");
        }
        return materialize_binding(handle, slot, desc);
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
            if (pinned) {
                slot = old_slot;
            } else {
                surface_bindings.erase(previous);
            }
        }
        if (slot_has_initialized_occupant(slot, handle)) {
            slot = next_slot++;
        }
        return materialize_binding(handle, slot, desc);
    }

    ImageT& materialize_binding(runtime::RenderSurfaceHandle handle,
                                std::size_t slot,
                                const runtime::SurfaceDesc& desc) {
        auto& physical = physical_surfaces[slot];
        owner_->stats.physical_surfaces_peak = std::max(
            owner_->stats.physical_surfaces_peak,
            static_cast<std::uint64_t>(physical_surfaces.size()));

        if (owner_->plan_preallocated && physical.image.image != VK_NULL_HANDLE) {
            if (physical.image.width != desc.width ||
                physical.image.height != desc.height) {
                throw std::invalid_argument(
                    "Vulkan plan-preallocated surface slot " +
                    std::to_string(slot) + " has dimensions " +
                    std::to_string(physical.image.width) + "x" +
                    std::to_string(physical.image.height) +
                    " but requested " + std::to_string(desc.width) + "x" +
                    std::to_string(desc.height));
            }
            physical.desc = desc;
            physical.image.text_atlas_encoding = desc.text_atlas_encoding;
            surface_bindings[handle] = slot;
            return physical.image;
        }

        if (physical.image.image == VK_NULL_HANDLE ||
            physical.image.width != desc.width ||
            physical.image.height != desc.height) {
            if (physical.image.image != VK_NULL_HANDLE) {
                owner_->destroy_image(physical.image);
            }
            owner_->make_image(physical.image, desc.width, desc.height, false,
                               owner_->to_vk_format(desc.format));
            physical.image.initialized = false;
        }
        physical.desc = desc;
        physical.image.text_atlas_encoding = desc.text_atlas_encoding;
        surface_bindings[handle] = slot;
        return physical.image;
    }
};

} // namespace chronon3d::backends::vulkan

#endif // CHRONON3D_ENABLE_VULKAN
