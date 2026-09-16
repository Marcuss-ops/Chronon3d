#pragma once

// ───────────────────────────────────────────────────────────────────────────
// daemon_render_job_payload.hpp — canonical RENDER_JOB payload decode.
//
// Phase 9 (CLI/IPC parity): the daemon wire JSON is decoded exactly once, at
// the IPC boundary, into the transport-neutral runtime::job::RenderJobRequest
// and then normalized once into NormalizedRenderJob. The render group
// receives the normalized job through RenderJobDispatcher and never re-parses
// the wire format. Keeping this decoder as the single canonical mapper (the
// render group holds no nlohmann::json dependency) makes a JSON re-forward or
// a second normalize step structurally impossible.
//
// The payload mirrors RenderingGen's renderJobPayload (renderinggen
// internal/chronon ipc.go): plan_path / assets_root / output / range_enabled
// + first_frame + last_frame / report / audio_source_path / encode_preset /
// receipt_verify / execution_requirements{backend,gpu_required,
// cpu_fallback_allowed,composition_required,video_source_required,
// packet_copy_allowed} / output_spec{codec,width,height,fps_num,fps_den}.
// Legacy top-level keys (codec, hardware_encoder, hardware, encoder_backend,
// ffmpeg_mode, gpu_hot_path_mode, rate_control_mode, crf, qp, bitrate,
// trace_output, trace_level, gop_copy_only, report, backend, fps_num,
// fps_den) remain accepted so pre-existing clients keep working.
//
// The vocabulary above is CLOSED and enforced by the decoder (the same
// authority as this comment): a key outside it is refused with a
// BadRequest diagnostic naming the key. Until that check existed this ingress
// was the tolerant one — the plan-file path is validated against a schema with
// `additionalProperties: false` — so the same typo was refused in one boundary
// and silently defaulted in the other. Adding a key to the payload therefore
// means adding it to the decoder's vocabulary as well.
// ───────────────────────────────────────────────────────────────────────────

#include <chronon3d/runtime/job/render_job_request.hpp>

#include <string>

namespace chronon3d::cli {

/// Decode the RENDER_JOB wire payload (JSON) into the semantic request.
/// This is the single canonical JSON→RenderJobRequest mapper on the daemon
/// path. Throws std::exception (nlohmann parse/type errors) on malformed
/// input; callers map that to ipc::Status::BadRequest.
[[nodiscard]] runtime::job::RenderJobRequest decode_render_job_payload(
    const std::string& payload);

} // namespace chronon3d::cli
