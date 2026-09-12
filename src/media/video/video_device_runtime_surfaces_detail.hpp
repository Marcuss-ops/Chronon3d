#include <chronon3d/runtime/cuda_surface_contract.hpp>

namespace chronon3d::media {

bool VideoDeviceRuntime::acquire_slot_surfaces(
    runtime::FrameExecutionSlot& slot,
    runtime::RenderSurfaceRegistry& registry,
    graph::RenderBackend& backend,
    std::uint32_t width,
    std::uint32_t height,
    std::string& reason) {
    reason.clear();
    if (width == 0 || height == 0) {
        reason = "invalid native video surface dimensions";
        return false;
    }
    if (!backend.supports_native_video_surface() ||
        !backend.supports_native_surfaces()) {
        reason = "backend does not support native video surfaces";
        return false;
    }
    if (!slot.transition_interop_state(runtime::InteropFrameState::Allocated)) {
        reason = "execution slot is not recyclable";
        return false;
    }

    auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::PipelineSlot);
    // This is the persistent FullGraph encode backing. DirectYUV owns a
    // separate NV12/P010 contract and never enters this pool.
    desc.kind = runtime::ResourceKind::Color;
    const auto contract = runtime::validate_cuda_surface_contract(desc);
    if (!contract.valid()) {
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        reason = contract.reason;
        return false;
    }

    // A slot owns one PipelineSlot backing across frames. Releasing the lease
    // returns the slot to Recyclable; it must not force a new Vulkan allocation
    // on the next acquisition. The source and encode roles intentionally alias
    // this same backing so the graph terminal output cannot reintroduce a
    // source→encode device copy.
    if (slot.native_surface != runtime::kInvalidRenderSurfaceHandle ||
        slot.source_surface != runtime::kInvalidRenderSurfaceHandle) {
        const auto encode_desc = registry.lookup(slot.native_surface);
        const auto source_desc = registry.lookup(slot.source_surface);
        const bool reusable = encode_desc && source_desc &&
            encode_desc->desc.width == desc.width &&
            encode_desc->desc.height == desc.height &&
            encode_desc->desc.format == desc.format &&
            source_desc->desc.width == desc.width &&
            source_desc->desc.height == desc.height &&
            source_desc->desc.format == desc.format &&
            slot.source_surface == slot.native_surface &&
            // The encode destination must remain bound in the backend across
            // the asynchronous NVENC ring.
            backend.is_native_surface_valid(slot.native_surface);
        if (reusable) {
            slot.backend = &backend;
            return true;
        }
        reason = "recyclable slot has stale or incompatible native surfaces";
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        return false;
    }

    const auto encode = registry.create(desc);
    if (encode == runtime::kInvalidRenderSurfaceHandle) {
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        reason = "surface registry rejected native video surface";
        return false;
    }

    const auto encode_result = backend.create_video_encode_surface(encode, desc);
    if (!encode_result.ok()) {
        registry.release(encode);
        (void)slot.transition_interop_state(runtime::InteropFrameState::Recyclable);
        reason = encode_result.error().message;
        return false;
    }

    slot.backend = &backend;
    slot.native_surface = encode;
    slot.source_surface = encode;
    return true;
}

} // namespace chronon3d::media
