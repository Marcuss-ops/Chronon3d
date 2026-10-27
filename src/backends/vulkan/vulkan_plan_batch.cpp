#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

void VulkanBackend::begin_plan_batch(const runtime::CommandPlan& plan) {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Audit N4 measurement: the two domains this site
    // still needs, timed (see Impl::DomainLock).
    Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
    m_impl->require_healthy();
    const bool trace_plan_bind = std::getenv("CHRONON3D_TRACE_PLAN_BIND") != nullptr;
    if (plan.pass_count() > Impl::FrameBatchState::kCompiledPassTimingCapacity) {
        throw std::length_error(
            "Vulkan compiled pass timing capacity exhausted");
    }
    begin_frame_batch_unlocked();

    // CommandPlan is the sole compiled synchronization authority. Vulkan
    // resolves transition.resource through plan.resources and translates the
    // canonical ResourceTransition stream directly to Synchronization2.
    m_impl->frame_batch.command_plan = &plan;

    // Audit N6: allocate the per-pass descriptor-set slots once, from the
    // compiled pass count, instead of letting push_back grow the vector on
    // pass-heavy graphs. begin_frame_batch_unlocked() cleared the vector but
    // kept its capacity, so this reserve grows it at most when a larger plan
    // arrives — no steady-state reallocation.
    m_impl->frame_batch.descriptor_sets.reserve(plan.pass_count());

    // P0 authority invariant: validate the complete compiled placement before
    // mutating VulkanSurfaceStore. The compatibility allocator is allowed to
    // pick slots only for explicitly unplanned surfaces; a compiled binding
    // must never reach the store's next_slot++ fallback.
    const auto invalid_slot = std::numeric_limits<std::size_t>::max();
    const auto& resources = plan.resources;
    auto& store = m_impl->surfaces;

    // Planned aliasing is explicit plan policy: register the plan's compiled
    // occupants per physical slot before the exact binds below. Aliasing is
    // then allowed only between handles THIS plan named, so an unplanned
    // occupant (e.g. the NVENC ring) still triggers the reserved-slot guard.
    store.clear_planned_aliases();
    for (const auto& allocation : resources.allocations) {
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle ||
            allocation.physical_slot == invalid_slot) {
            continue;
        }
        store.register_planned_occupant(allocation.physical_slot,
                                        allocation.surface);
    }

    if (std::getenv("CHRONON3D_TRACE_PLAN_BIND") != nullptr) {
        std::fprintf(stderr, "[planbind] slots=%zu allocations=%zu\n",
                     resources.slots.size(), resources.allocations.size());
        for (const auto& allocation : resources.allocations) {
            std::fprintf(stderr,
                         "[planbind]   alloc surface=%llu slot=%zu req=%u desc=%ux%u life=%d\n",
                         static_cast<unsigned long long>(allocation.surface),
                         allocation.physical_slot,
                         static_cast<unsigned>(allocation.request_index),
                         resources.requests[allocation.request_index].desc.width,
                         resources.requests[allocation.request_index].desc.height,
                         static_cast<int>(resources.requests[allocation.request_index].desc.lifetime));
        }
        for (const auto& [handle, slot] : store.surface_bindings) {
            const auto phys = store.physical_surfaces.find(slot);
            std::fprintf(stderr,
                         "[planbind]   bind  surface=%llu slot=%zu init=%d\n",
                         static_cast<unsigned long long>(handle), slot,
                         phys != store.physical_surfaces.end()
                             ? static_cast<int>(phys->second.image.initialized)
                             : -1);
        }
    }

    // Keep this plan's logical bindings alive while retiring completed
    // previous-frame transients. In particular, host-uploaded carriers must
    // remain bound until the content-aware placement map has been derived.
    auto& protected_plan_handles = m_impl->plan_protected_handles_scratch_;
    protected_plan_handles.clear();
    protected_plan_handles.reserve(resources.requests.size());
    for (const auto& request : resources.requests) {
        if (request.surface != runtime::kInvalidRenderSurfaceHandle) {
            protected_plan_handles.push_back(request.surface);
        }
    }
    // A command batch may contain several independently compiled plans whose
    // surfaces were all created/uploaded before recording starts. Retiring
    // here would drop the carriers belonging to the later plans.
    if (!m_impl->command_batch_active) {
        m_impl->retire_completed_frame_transient_surfaces(protected_plan_handles);
    }

    // ResourcePlanner's physical_slot is a plan-local namespace. Resolve it
    // into backend slots here, after inspecting the content already resident
    // in each logical surface. A plan slot may alias transient producers, but
    // it must never merge two initialized carriers whose bytes came from the
    // host or another external producer.
    auto& plan_slot_allocations = m_impl->plan_slot_allocations_scratch_;
    auto& slot_offsets = m_impl->plan_slot_allocation_offsets_scratch_;
    auto& slot_cursor = m_impl->plan_slot_allocation_cursor_scratch_;
    slot_offsets.assign(resources.slots.size() + 1, 0);
    for (std::size_t allocation_index = 0;
         allocation_index < resources.allocations.size(); ++allocation_index) {
        const auto& allocation = resources.allocations[allocation_index];
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle ||
            allocation.physical_slot == invalid_slot) {
            continue;
        }
        if (allocation.request_index >= resources.requests.size()) {
            throw std::logic_error(
                "Vulkan compiled allocation references an invalid request index");
        }
        if (allocation.physical_slot >= resources.slots.size()) {
            throw std::logic_error(
                "Vulkan compiled allocation references a physical slot outside the plan");
        }

        const auto& request = resources.requests[allocation.request_index];
        if (request.surface != runtime::kInvalidRenderSurfaceHandle &&
            request.surface != allocation.surface) {
            throw std::logic_error(
                "Vulkan compiled allocation surface disagrees with its ResourceRequest");
        }
        if (store.unplanned_surface_handles.contains(allocation.surface)) {
            throw std::logic_error(
                "Vulkan compiled allocation attempts to claim an explicitly unplanned surface");
        }
        ++slot_offsets[allocation.physical_slot + 1];
    }
    for (std::size_t slot = 0; slot < resources.slots.size(); ++slot)
        slot_offsets[slot + 1] += slot_offsets[slot];
    slot_cursor.assign(slot_offsets.begin(), slot_offsets.end() - 1);
    plan_slot_allocations.resize(resources.allocations.size());
    for (std::size_t allocation_index = 0;
         allocation_index < resources.allocations.size(); ++allocation_index) {
        const auto& allocation = resources.allocations[allocation_index];
        const auto slot = allocation.physical_slot;
        if (allocation.surface != runtime::kInvalidRenderSurfaceHandle &&
            slot != invalid_slot && slot < resources.slots.size())
            plan_slot_allocations[slot_cursor[slot]++] = allocation_index;
    }

    // Fresh backend slots live after every slot already handed out by the
    // compatibility allocator. They are independent from the plan-local
    // indices above, so separate plans in one command batch cannot collide.
    store.next_slot = std::max(store.next_slot, resources.slots.size());

    auto& slot_map = m_impl->plan_slot_map_scratch_;
    slot_map.assign(resources.allocations.size(), invalid_slot);
    for (std::size_t plan_slot = 0; plan_slot < resources.slots.size(); ++plan_slot) {
        const auto first_allocation = slot_offsets[plan_slot];
        const auto last_allocation = slot_offsets[plan_slot + 1];
        if (first_allocation == last_allocation) continue;

        auto& carriers = m_impl->plan_carriers_scratch_;
        carriers.clear();
        carriers.reserve(last_allocation - first_allocation);
        for (auto position = first_allocation; position < last_allocation; ++position) {
            const auto allocation_index = plan_slot_allocations[position];
            const auto handle = resources.allocations[allocation_index].surface;
            const auto binding = store.surface_bindings.find(handle);
            if (binding == store.surface_bindings.end()) continue;
            const auto physical = store.physical_surfaces.find(binding->second);
            if (physical != store.physical_surfaces.end() &&
                physical->second.image.initialized) {
                carriers.push_back(allocation_index);
            }
        }

        if (carriers.size() >= 2) {
            // Two live carriers cannot share one VkImage. Keep each carrier's
            // current slot. A producer may join a carrier only when it starts
            // after the carrier's final read; the opposite ordering would
            // overwrite host content before its first consumer.
            auto& carrier_slots = m_impl->plan_carrier_slots_scratch_;
            carrier_slots.clear();
            for (const auto allocation_index : carriers) {
                const auto handle = resources.allocations[allocation_index].surface;
                const auto binding = store.surface_bindings.find(handle);
                if (binding == store.surface_bindings.end()) {
                    throw std::logic_error(
                        "Vulkan content-aware placement lost a carrier binding");
                }
                if (std::find(carrier_slots.begin(), carrier_slots.end(), binding->second) != carrier_slots.end()) {
                    throw std::logic_error(
                        "Vulkan initialized carriers already share a physical slot");
                }
                carrier_slots.push_back(binding->second);
                slot_map[allocation_index] = binding->second;
            }
            auto& deferred_producers = m_impl->plan_deferred_producers_scratch_;
            deferred_producers.clear();
            for (auto position = first_allocation; position < last_allocation; ++position) {
                const auto allocation_index = plan_slot_allocations[position];
                if (slot_map[allocation_index] != invalid_slot) continue;
                const auto& request = resources.requests.at(
                    resources.allocations[allocation_index].request_index);
                for (const auto carrier_index : carriers) {
                    const auto& carrier_request = resources.requests.at(
                        resources.allocations[carrier_index].request_index);
                    if (carrier_request.last < request.first) {
                        slot_map[allocation_index] = slot_map[carrier_index];
                        break;
                    }
                }
                if (slot_map[allocation_index] == invalid_slot) {
                    deferred_producers.push_back(allocation_index);
                }
            }
            if (!deferred_producers.empty()) {
                std::size_t producer_slot = invalid_slot;
                for (const auto allocation_index : deferred_producers) {
                    const auto binding = store.surface_bindings.find(
                        resources.allocations[allocation_index].surface);
                    if (binding != store.surface_bindings.end()) {
                        producer_slot = binding->second;
                        break;
                    }
                }
                if (producer_slot == invalid_slot) {
                    producer_slot = store.next_slot++;
                    while (store.physical_surfaces.contains(producer_slot) ||
                           store.slot_in_use(producer_slot)) {
                        producer_slot = store.next_slot++;
                    }
                }
                for (const auto allocation_index : deferred_producers) {
                    slot_map[allocation_index] = producer_slot;
                }
            }
            continue;
        }

        auto& deferred_producers = m_impl->plan_deferred_producers_scratch_;
        deferred_producers.clear();
        if (!carriers.empty()) {
            const auto carrier_index = carriers.front();
            const auto carrier_slot = store.surface_bindings.at(
                resources.allocations[carrier_index].surface);
            slot_map[carrier_index] = carrier_slot;
            for (auto position = first_allocation; position < last_allocation; ++position) {
                const auto allocation_index = plan_slot_allocations[position];
                if (allocation_index == carrier_index) continue;
                const auto& carrier_request = resources.requests.at(
                    resources.allocations[carrier_index].request_index);
                const auto& producer_request = resources.requests.at(
                    resources.allocations[allocation_index].request_index);
                if (carrier_request.last < producer_request.first) {
                    slot_map[allocation_index] = carrier_slot;
                } else {
                    deferred_producers.push_back(allocation_index);
                }
            }
        } else {
            // Preserve an existing logical binding where possible. This keeps
            // repeated plans stable and avoids growing the backend slot set.
            for (auto position = first_allocation; position < last_allocation; ++position) {
                const auto allocation_index = plan_slot_allocations[position];
                const auto binding = store.surface_bindings.find(
                    resources.allocations[allocation_index].surface);
                if (binding != store.surface_bindings.end()) {
                    slot_map[allocation_index] = binding->second;
                    break;
                }
            }
            for (auto position = first_allocation; position < last_allocation; ++position) {
                const auto allocation_index = plan_slot_allocations[position];
                if (slot_map[allocation_index] == invalid_slot) {
                    deferred_producers.push_back(allocation_index);
                }
            }
        }

        if (!deferred_producers.empty()) {
            std::size_t producer_slot = invalid_slot;
            for (const auto allocation_index : deferred_producers) {
                const auto binding = store.surface_bindings.find(
                    resources.allocations[allocation_index].surface);
                if (binding != store.surface_bindings.end()) {
                    producer_slot = binding->second;
                    break;
                }
            }
            if (producer_slot == invalid_slot) {
                producer_slot = store.next_slot++;
                while (store.physical_surfaces.contains(producer_slot) ||
                       store.slot_in_use(producer_slot)) {
                    producer_slot = store.next_slot++;
                }
            }
            for (const auto allocation_index : deferred_producers) {
                slot_map[allocation_index] = producer_slot;
            }
        }
    }

    auto& backend_slot_occupants = m_impl->plan_occupants_scratch_;
    backend_slot_occupants.clear();
    backend_slot_occupants.reserve(resources.allocations.size());
    for (std::size_t allocation_index = 0;
         allocation_index < resources.allocations.size(); ++allocation_index) {
        const auto mapped_slot = slot_map[allocation_index];
        if (mapped_slot == invalid_slot) continue;
        backend_slot_occupants.emplace_back(mapped_slot,
            resources.allocations[allocation_index].surface);
    }
    std::sort(backend_slot_occupants.begin(), backend_slot_occupants.end());
    const auto occupants_for = [&](std::size_t slot) {
        const auto first = std::lower_bound(backend_slot_occupants.begin(), backend_slot_occupants.end(), slot,
            [](const auto& occupant, std::size_t value) { return occupant.first < value; });
        const auto last = std::upper_bound(first, backend_slot_occupants.end(), slot,
            [](std::size_t value, const auto& occupant) { return value < occupant.first; });
        return std::pair{first, last};
    };

    if (trace_plan_bind) {
        for (std::size_t allocation_index = 0;
             allocation_index < resources.allocations.size(); ++allocation_index) {
            if (slot_map[allocation_index] == invalid_slot) continue;
            spdlog::info("[planbind] surface={} plan_slot={} backend_slot={} initialized={}",
                         resources.allocations[allocation_index].surface,
                         resources.allocations[allocation_index].physical_slot,
                         slot_map[allocation_index],
                         store.physical_surfaces.contains(slot_map[allocation_index]) &&
                             store.physical_surfaces.at(slot_map[allocation_index])
                                 .image.initialized);
        }
    }

    // Validate against pre-bound surfaces outside this plan. Handles listed in
    // the same mapped occupant set are intentional aliases and are permitted;
    // an initialized slot owned by any other logical handle is a real clash.
    for (const auto& [bound_handle, bound_slot] : store.surface_bindings) {
        const auto [first, last] = occupants_for(bound_slot);
        if (first == last) continue;
        const auto physical = store.physical_surfaces.find(bound_slot);
        if (physical == store.physical_surfaces.end() || !physical->second.image.initialized)
            continue;
        if (!std::binary_search(first, last, std::pair{bound_slot, bound_handle})) {
            throw std::logic_error(
                "Vulkan compiled physical-slot collision with initialized content");
        }
    }

    for (std::size_t allocation_index = 0;
         allocation_index < resources.allocations.size(); ++allocation_index) {
        const auto& allocation = resources.allocations[allocation_index];
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle ||
            allocation.physical_slot == invalid_slot) {
            continue;
        }
        const auto mapped_slot = slot_map[allocation_index];
        const auto& planned = resources.slots[allocation.physical_slot];
        const auto& request_desc = resources.requests[allocation.request_index].desc;
        const auto width = request_desc.width != 0 ? request_desc.width : planned.desc.width;
        const auto height = request_desc.height != 0 ? request_desc.height : planned.desc.height;
        const auto format = request_desc.format.pixel != runtime::PixelFormat::Unknown
            ? request_desc.format : planned.desc.format;
        const auto usage = request_desc.usage != runtime::ResourceUsage::Generic
            ? request_desc.usage : planned.desc.usage;
        const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
            width, height, format, usage, request_desc.lifetime);

        const auto compatibility_cursor = store.next_slot;
        const auto [first, last] = occupants_for(mapped_slot);
        // Protected handles are no longer needed after transient retirement;
        // reuse that CPU scratch for the allowed handle span.
        auto& allowed_handles = m_impl->plan_protected_handles_scratch_;
        allowed_handles.clear();
        for (auto occupant = first; occupant != last; ++occupant)
            allowed_handles.push_back(occupant->second);
        const std::span<const runtime::RenderSurfaceHandle> allowed_occupants(allowed_handles);
        m_impl->bind_surface_to_slot(
            allocation.surface, mapped_slot, desc, allowed_occupants);

        const auto actual = store.surface_bindings.find(allocation.surface);
        if (actual == store.surface_bindings.end() ||
            actual->second != mapped_slot) {
            throw std::logic_error(
                "Vulkan compiled surface was not materialized in its planned slot");
        }
        if (store.next_slot != compatibility_cursor) {
            throw std::logic_error(
                "Vulkan compiled surface reached the dynamic compatibility allocator");
        }
    }
    m_impl->reclaim_unused_transient_surface_slots();

    // Audit N4: lease the whole compiled surface set for the batch. Every pass
    // then records with the surface domain released — resolve_image() and the
    // compiled transition stream both read the lease instead of the store —
    // and the plan-wide set is unleased when the batch ends. The scratch
    // vector is reused so this costs no per-frame allocation.
    auto& lease_handles = m_impl->plan_lease_scratch_;
    lease_handles.clear();
    lease_handles.reserve(resources.requests.size());
    for (const auto& request : resources.requests) {
        if (request.surface != runtime::kInvalidRenderSurfaceHandle) {
            lease_handles.push_back(request.surface);
        }
    }
    m_impl->pin_recording_leases_locked(lease_handles);
#else
    (void)plan;
    unsupported("begin_plan_batch");
#endif
}

} // namespace chronon3d::backends::vulkan
