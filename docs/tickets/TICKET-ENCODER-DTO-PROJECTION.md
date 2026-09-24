# TICKET-ENCODER-DTO-PROJECTION — encoder contract projections (change-amplification map)

| **TICKET-ENCODER-DTO-PROJECTION** | |
| --- | --- |
| **Stato** | DOCUMENTED (protocol); no code change — the projections are allowed by design |
| **Priorità** | P2 (structural; audit F16) |
| **Problema** | The encoder contract is represented by 3+ parallel DTOs with explicit, commented conversions. Projections are permitted (each layer has a different authority), but nothing recorded *which* structs and converters a new encoder field must traverse, so adding one field is an undocumented 3-4 struct + 2 converter edit. A forgotten hop is silent: the field is accepted at the CLI and dropped before the encoder. |
| **Confine** | Read-only documentation of the existing chain + the extension protocol. No signature, fingerprint or wire-format change. Any *consolidation* of the DTOs would move encoding fingerprints and is out of scope (needs its own ticket + parity evidence). |

## The chain (current, authoritative)

```text
CLI args / daemon IPC / render-plan JSON
  └─ RenderJobOptions                          include/chronon3d/timeline/render_job.hpp
       ├─ video_settings: RenderVideoOptions   (encoder_backend, hardware_encoder,
       │                                        codec, rate_control_mode, crf, qp,
       │                                        bitrate, encode_preset, tune, async_depth)
       └─ output_spec / execution_requirements
  └─ FfmpegExportOptions                       commands/video/common/*_options.hpp
       ├─ EncoderOptions      (encoder_options.hpp)          ← FfmpegPipeOptions projection
       ├─ OutputOptions       (output_options.hpp)
       ├─ SinkOptions         (sink_options.hpp)
       ├─ PipeOptions         (pipe_options.hpp)
       └─ WarmupOptions       (warmup_options.hpp)
  └─ FfmpegPipeOptions                          utils/video/ffmpeg_pipe_encoder.hpp
  └─ VideoSinkConfig (+ build_sink_config())     utils/video/video_sink_adapter.hpp
  └─ EncoderConfigRequest → ResolvedEncoderConfig
         utils/video/encoder_config_resolution.{hpp,cpp}   ← the only encoder-policy authority
```

## Adding a new encoder field — the protocol

A new encoder knob must be wired along the whole chain, in this order, and each hop
must be covered by the wire-fidelity test before it counts as landed:

1. **CLI option** — `commands/render/register_render_commands.cpp` (the `.inc`
   support fragment was absorbed into the owning TU in the Hotspot Wave A
   decomposition), with a `CLI::IsMember` gate generated from a canonical
   vocabulary where one exists.
2. **Job contract** — `RenderVideoOptions` in `include/chronon3d/timeline/render_job.hpp`
   (this is the CLI/IPC parity surface; a field missing here desynchronises the two
   transports and their fingerprints).
3. **Export DTO** — the relevant sub-struct of `FfmpegExportOptions`, plus the
   projection that fills it from the job contract
   (`utils/video/video_job_execute_options.cpp`).
4. **Request/policy** — `EncoderConfigRequest` + the resolution matrix in
   `encoder_config_resolution.cpp` when the knob changes *encoder policy* (rate
   control, preset, tune, async depth). Encoders apply `ResolvedEncoderConfig`
   verbatim and never reinterpret policy themselves.
5. **Sink projection** — `build_sink_config()` declared in
   `utils/video/video_sink_adapter.hpp` (defined in
   `video_sink_adapter_config.cpp`) when the knob reaches the sink/pipe boundary.
6. **Wire fidelity test** — `tests/cli/test_render_job_planning.cpp`
   (CLI/IPC parity) and `tests/cli/test_encoder_config_resolution.cpp`
   (resolution matrix). A new field is not "landed" until one of them observes it.

## Acceptance criteria

1. The chain above matches the tree (verified by reading the six named files).
2. The protocol lists every hop a field must traverse, with a named test per hop.
3. No encoder field is added without a wire-fidelity observation (AGENTS.md honesty
   rule: do not mark a suite green that returns failure).

## Notes

- `ResolvedEncoderConfig` is fail-closed: an unknown rate-control string is rejected,
  never silently downgraded (see the module header of `encoder_config_resolution`).
  Keep new knobs on that path rather than adding per-backend interpretation.
- The `encoder_backend` selector vocabulary is now single-sourced (audit F4):
  `media::kEncoderTransportNative` / `kEncoderTransportPipe` in
  `include/chronon3d/media/video/video_execution_resolver.hpp`, aliased by the CLI.
  Do not respell those literals when adding a transport — extend
  `EncoderTransport` + `encoder_transport_names()` instead, and the CLI option gate
  picks it up automatically.
