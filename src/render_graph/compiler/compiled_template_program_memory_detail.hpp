// ── Fase C — physical resource analysis ─────────────────────────────────────

PhysicalResourcePlan analyze_physical_resources(
    const CompiledFrameGraph& compiled,
    std::uint32_t bytes_per_pixel) {
    PhysicalResourcePlan plan;
    const auto& alloc = compiled.resource_table();

    plan.slot_count        = alloc.physical_slot_count;
    plan.peak_live_resources = alloc.peak_live_resource_count;
    plan.aliasable_resources = alloc.aliasable_resource_count;
    plan.logical_resources   = alloc.logical_resource_count;
    plan.excluded_persistent = alloc.excluded_persistent_count;
    plan.excluded_async      = alloc.excluded_async_count;

    plan.sized_slots.reserve(alloc.slots.size());
    for (std::size_t slot_index = 0; slot_index < alloc.slots.size(); ++slot_index) {
        const auto& slot = alloc.slots[slot_index];
        SizedPhysicalSlot sized;
        sized.slot_id      = static_cast<std::uint32_t>(slot_index);
        sized.max_width    = slot.desc.width;
        sized.max_height   = slot.desc.height;
        sized.surface_bytes = slot.capacity_bytes != 0
            ? static_cast<std::uint64_t>(slot.capacity_bytes)
            : static_cast<std::uint64_t>(slot.desc.width) *
              slot.desc.height * bytes_per_pixel;
        plan.peak_transient_bytes += sized.surface_bytes;
        plan.sized_slots.push_back(sized);
    }

    return plan;
}

DeviceMemoryPlan build_device_memory_plan(
    const PhysicalResourcePlan& resources,
    std::uint64_t decoder_budget,
    std::uint64_t encoder_budget,
    std::uint64_t atlas_budget,
    std::uint64_t scratch_budget) {
    DeviceMemoryPlan plan;
    plan.physical_slots     = resources.peak_transient_bytes;
    plan.frame_slot_buffers = resources.peak_transient_bytes;  // per-slot ring = peak transient
    plan.decoder_budget     = decoder_budget;
    plan.encoder_budget     = encoder_budget;
    plan.atlas_budget       = atlas_budget;
    plan.scratch_budget     = scratch_budget;

    // Safety margin = 15% of non-margin categories
    const auto base = plan.physical_slots + plan.frame_slot_buffers +
                      plan.persistent_assets + plan.baked_surfaces +
                      plan.decoder_budget + plan.encoder_budget +
                      plan.atlas_budget + plan.scratch_budget;
    plan.safety_margin = static_cast<std::uint64_t>(
        static_cast<double>(base) * DeviceMemoryPlan::kDefaultSafetyFraction);
    plan.estimated_peak = base + plan.safety_margin;

    return plan;
}

AdmissionResult admit_or_degrade_job(
    const DeviceMemoryPlan& plan,
    std::uint64_t available_vram) {
    AdmissionResult result;
    result.plan = plan;
    result.available_vram = available_vram;

    if (plan.estimated_peak <= available_vram) {
        result.verdict    = AdmissionVerdict::Admitted;
        result.diagnostic = "ADMITTED";
    } else {
        result.verdict    = AdmissionVerdict::Rejected;
        result.diagnostic = "REJECTED: estimated_peak (" +
                            std::to_string(plan.estimated_peak) +
                            ") > available_vram (" +
                            std::to_string(available_vram) + ")";
    }

    return result;
}
