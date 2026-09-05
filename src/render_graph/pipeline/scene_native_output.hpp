// scene_native_output.hpp — internal declarations for native output
// synchronization and native encode-source residency, extracted from
// scene.cpp. Not installed and not part of any public API.

#pragma once

#include "scene_internal.hpp"

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>

#include <memory>

namespace chronon3d::graph::detail {

// Terminal synchronization of the rendered framebuffer against the backend's
// native surface (see scene.cpp phase 11 for the contract).
void synchronize_native_output(RenderGraphContext& ctx,
                               const std::shared_ptr<Framebuffer>& framebuffer);

// The graph may materialize a per-frame FrameTransient output before the
// video exporter copies it into the persistent execution-slot encode surface.
// Once that device-to-device copy has been recorded, the temporary graph
// handle is no longer needed. Never release either persistent handoff surface.
void release_temporary_native_output(RenderGraphContext& ctx,
                                     const std::shared_ptr<Framebuffer>& framebuffer,
                                     runtime::RenderSurfaceHandle source);

// Owns the native encode frame batch and the residency of the frame's native
// source surface (upload, reuse, release) across the scene render phases.
class NativeSourceResidency {
public:
    NativeSourceResidency(RenderGraphContext& ctx, RenderBackend& backend);

    // Starts the backend frame batch when a native encode surface is the
    // output contract; a no-op when one is already active.
    void begin_encode_batch();

    // Unconditionally starts the backend frame batch before graph execution
    // when no batch is active yet (frame-batching boundary, phase 11).
    void begin_frame_batch();

    [[nodiscard]] bool encode_batch_active() const noexcept {
        return encode_batch_active_;
    }

    // Whether any active layer uses 2.5D projection or native 3D; projected
    // surfaces force full-frame source uploads instead of dirty-rect clips.
    bool has_projected_surface{false};

    // Source-space clip applied to the next persistent-source upload. Dirty
    // rects may restrict the upload; projected surfaces force a full-frame
    // clip (set from the layer scan in phase 6 of scene.cpp).
    void set_upload_clip(std::optional<raster::BBox> clip) {
        native_source_upload_clip_ = std::move(clip);
    }

    // Resolves (creating or uploading as needed) the native source surface
    // for the framebuffer. Returns kInvalidRenderSurfaceHandle on failure.
    [[nodiscard]] runtime::RenderSurfaceHandle ensure_native_source(
        const std::shared_ptr<Framebuffer>& framebuffer, Frame frame);

    // Early-exit path: copy a reused framebuffer into the encode surface and
    // close the frame batch. Returns nullptr when the copy failed.
    [[nodiscard]] std::shared_ptr<Framebuffer> finish_reused_native_frame(
        const std::shared_ptr<Framebuffer>& framebuffer, Frame frame);

    // Full-path exit: copy the executed framebuffer into the encode surface
    // and close the frame batch. Returns nullptr when the copy failed.
    [[nodiscard]] std::shared_ptr<Framebuffer> finish_frame_encode(
        std::shared_ptr<Framebuffer> exec_fb, Frame frame);

private:
    RenderGraphContext& ctx_;
    RenderBackend& backend_;
    bool encode_batch_active_{false};
    std::optional<raster::BBox> native_source_upload_clip_;
};

} // namespace chronon3d::graph::detail
