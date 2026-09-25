# TICKET-ZERO-COPY-ZWAVES — Zero-copy Z-wave census and execution plan

Status: `ACTIVE` (code waves landed; GPU certification remains host-dependent)
Owner: media/video + rendering-core maintainers
Created: 2026-09-07
Baseline: `main@23108742a1d72bd9a6eb6ca208506965f4f40154`
Gates: [`docs/ZERO_COPY_GATES.md`](../ZERO_COPY_GATES.md)

Latest safe optimization: DirectYUV text preparation now reuses immutable
MTSDF atlases through the daemon-lifetime `DirectYuvTextTextureCache`. This
removes repeated font loading/rasterization between short-lived programs but
does not change the zero-copy gate semantics: CUDA image residency and upload
accounting remain owned by `VideoDeviceRuntime`.

This ticket executes the Z0–Z7 zero-copy plan (architecture working session,
2026-09-07) against the *current* codebase. The plan's principle:

> One backing allocation per output slot, many API views, one lifecycle, one
> compositor authority, one encoder authority. Not "make the existing pipeline
> zero-copy", but remove the natural place where a copy could happen.

It also records the Z0 copy census (which waves deliberately change no
behavior) and re-maps each wave onto the real, evolved code. The codebase has
moved past several plan starting points (single `CudaNv12Compositor` authority
exists; DirectYuv lane with `composite_direct_nv12_batch` exists;
`FrameSlotPool`/`FrameExecutionSlot`/`GpuCompletionTracker` are the slot
protocol), so each wave below states what is **already satisfied** and what
**remains**.

---

## Z0 — Copy census (DONE, 2026-09-07)

Every gate counter, its authoritative increment site, the lane that triggers
it, and the wave that must zero it:

| Gate | Counter | Instrumented at (authority) | Certified FullGraph native lane today | Zeroed by |
|---|---|---|---|---|
| 1 host_upload | `gpu_upload_bytes` | Vulkan backend `Impl::upload/upload_region` (`vulkan_surface_store_private.cpp`) → `vulkan_backend_stats.cpp` export | 0 in steady state (atlas/asset uploads are warmup or CUDA-side, see F5) | Z7 (two-window cert) |
| 2 host_readback | `gpu_readback_bytes` | Vulkan backend `Impl::download` → stats export | 0 (video lane is `mark_gpu_authoritative`; silent readback refused) | — (already 0) |
| 3 nv12_to_rgba | `nv12_to_rgba_frames` | `CudaNv12Compositor::composite()` (`compositor/cuda/cuda_nv12_compositor_paths.cpp`) | 0 on DirectYuv; >0 per decoded frame on VulkanGraph lanes (RGBA video texture) | Z4 (certified set via direct lane) |
| 4 rgba_to_nv12 | `rgba_to_nv12_frames` | `CudaNv12Compositor::composite_surface_to_nv12()` (same file) | **>0 per frame** — `NativeAvEncoder::write_native_surface_impl` converts the slot's RGBA encode surface into a CUDA NV12 hwframe | Z4 |
| 5 encoder_staging | `encoder_staging_copy_bytes` | CPU lanes only: `NativeAvSink` (`encode/native_av_sink.cpp`), `RawVideoSink` | 0 on CUDA lanes (kernels write directly); CPU fallback is refused under `require_native_gpu` | — (already 0 on certified lane) |
| 6 gpu_surface_copy | `gpu_surface_copy_frames` | `VulkanBackend::Impl::copy_surface_to_cuda_encoder` (`vulkan_backend_cuda_export_private.cpp`) | **>0 per frame** — graph D2D copy source→encode surface in `scene_native_output.cpp` `finish_frame_encode` (and the writer's unprepared-handoff blit) | Z3 |
| 7 cpu_pixel_readback | `cpu_pixel_readback_bytes` / `cpu_pixel_readback_count` | **was vacuous**; instrumented 2026-09-07 at the single Vulkan download authority (`Impl::download`) | 0 (lane never downloads) — now an honest canary | — (already 0; canary) |
| 8 video_surface_upload | `video_surface_upload_bytes` | graph retained-source upload path (`scene_native_output.cpp` `NativeSourceResidency`) | 0 when the graph output is already native (no host upload) | — (already 0 on native lane) |
| 9 native_surface_frames | `gpu_native_surface_frames` | writer (`pipe_export_writer.cpp`) | >0 on certified lane | — (positive gate) |
| 10 native_encode_frames | `gpu_native_encode_frames` | writer | >0 on certified lane | — (positive gate) |

### Findings (all code-referenced)

1. **F1 — `require_gpu_native` plans `VulkanCopy` by design.** The resolver
   (`src/media/video/video_execution_resolver.cpp`) maps
   `GpuHotPathMode::RequireGpuNative` →
   `{DecodePath::Nvdec, CompositePath::VulkanGraph, EncodePath::Nvenc,
   InteropPath::VulkanCuda, SurfaceHandoffPath::VulkanCopy}` and the unit tests
   pin that mapping (`tests/cli/test_packet_assembler_contract.cpp`,
   `tests/cli/test_ffmpeg_export_options.cpp`). Therefore the certification
   identity gate `surface_handoff_path == direct` **cannot pass on the FullGraph
   lane as currently defined** — it is a plan-axis consequence, not a bug.
2. **F2 — FullGraph native lane does two real GPU passes per frame** that the
   gates measure: (a) a device-to-device copy of the rendered RGBA surface into
   the slot's CUDA-exportable encode surface (`scene_native_output.cpp`
   `finish_frame_encode` → `copy_surface_to_cuda_encoder`, gate 6), and
   (b) an RGBA→NV12 CUDA conversion into a distinct FFmpeg hwframe
   (`native_av_encoder_native_surface.cpp`, gate 4). Physical slots today hold
   two allocations: the encode RGBA surface + the FFmpeg NV12 pool buffer
   (`kCudaEncodeRingSlots = 6`). This is the plan's Z3/Z4 demolition target.
3. **F3 — gates 7 was vacuous.** `cpu_pixel_readback_count` / `_bytes` were
   declared and exported to the sidecar but never incremented. Fixed at the
   single GPU→CPU pixel authority (`Impl::download`).
4. **F4 — the certification scene is dead on current main.**
   `TextPlaceAnimatedCenter` is not registered anywhere in this repository
   (content/ compositions were externalized; the CLI always registers only
   `DarkGridBackground` and `CameraImageClip`, see
   `apps/chronon3d_cli/register_runtime_compositions.cpp`). The tool default was
   re-pointed to the asset-free builtin `DarkGridBackground`
   (`tools/verify_zero_copy_end_to_end.sh`, `docs/ZERO_COPY_GATES.md`);
   `CHRONON3D_BENCH_SCENE` still overrides on hosts with a content pack.
5. **F5 — gate 1 coverage gap on the CUDA lane.** `gpu_upload_bytes` is fed only
   by the Vulkan backend upload stats; `cuMemcpyHtoD` uploads inside
   `VideoDeviceRuntime`'s CUDA image cache (`get_or_upload_image`) are invisible
   to gate 1. On the DirectYuv lane (overlay/watermark/subtitle images) these
   uploads would not be measured. Z7 must either count them or formalize the
   prewarm/steady-state windows (plan §9) without reclassifying the counter.
6. **F6 — the DirectYuv lane already implements the plan's fused-direct write.**
   `NativeAvEncoder::write_direct_yuv` runs
   `CudaNv12Compositor::composite_direct_nv12_batch` (per-pixel YUV↔RGB math,
   no full-frame RGBA intermediate, no separate conversion pass) straight into
   an FFmpeg CUDA NV12 frame of the bounded queue — gates 3/4/6 stay 0 there by
   construction. It requires an NV12/P010 decoded background, so it is the
   zero-copy lane for decode+overlay compositions, not for arbitrary synthetic
   scenes.
7. **F7 — two-window semantics needed.** Atlas/asset uploads that occur during
   the warmup frames are inside the measured job; the certification already
   passes `--warmup-renderer --warmup-framebuffers 10`, but nothing separates
   "setup/prewarm" from "steady-state" counters in the report. Plan §9's two
   windows should be formalized without changing counter meaning.

---

## Z1 — Encode-output role + native backing/view contract (PARTIAL: present)

The plan's abstract role vocabulary (`VideoEncodeOutput`, `PixelFormatClass`,
`Lifetime`, `Access`, `InteropRequirement`) maps onto authorities that already
exist and must **not** be duplicated:

- output slots with one physical encode surface + lifecycle:
  `runtime::FrameExecutionSlot` + `InteropFrameState` + `FrameSlotPool` +
  `GpuCompletionTracker` (runtime/frame/);
- one NV12 compositor authority: `media::video::CudaNv12Compositor` +
  `VideoCompositor` registry/resolver facade (`src/media/video/compositor/`);
- one encoder resolver/authority: `create_video_encoder` +
  `NativeAvEncoder` (`apps/chronon3d_cli/utils/video/`), with
  `IVideoEncoder::write_direct_yuv / write_native_surface / write_frame`
  capability surface;
- orthogonal plan axes already abstract:
  `media::VideoExecutionPlan{decode, composite, encode, interop, handoff}`
  (`video_execution_resolver.hpp`).

Remaining Z1 work: **typed encoder capability split**. The plan forbids
accidentally feeding CPU RGBA to the native endpoint and forbids a
`maybe-CPU/maybe-CUDA/maybe-Vulkan` God interface. Today the capability surface
is virtual-methods-with-defaults on one `IVideoEncoder`; the strict typed split
(native-surface implementation vs host-frame fallback, chosen by the single
resolver) is the follow-up, alongside the effect/compositor capability matrix
(supports(effect, NativeYuvComposition), plan §5) feeding the canonical
`VideoCompositor` resolver — no new resolver.

## Z2 — Encoder consumes the output backing directly; per-slot persistent registration (MOSTLY PRESENT)

Already satisfied on the certified lanes: external-memory import happens once
per destination surface (`cuda_nv12_compositors_` map keyed by surface, reused
for the whole job ⇒ `external_memory_import_count ≈ output_slots`), the CUDA
encode queue is a bounded ring (`kCudaEncodeRingSlots = 6`), NVENC consumes
FFmpeg CUDA hwframes from a reusable pool, and gate 5 (`encoder_staging_copy_bytes`)
is 0 wherever the native path runs. The remaining delta is F2's *second
allocation*: FFmpeg `av_hwframe_get_buffer` per frame pulls an NV12 buffer that
is not the slot's own exportable allocation. Making FFmpeg represent the same
allocation (or registering NVENC on the slot backing directly) is the deep
interop follow-up; do not proceed until Z3's equivalence gate is defined.

## Z3 — CudaNv12Compositor writes into the same encode backing; remove the per-frame D2D copy (SEAM + GATE LANDED; pixel binding PENDING GPU)

Target: gate 6 → 0 by removing the per-frame device-to-device copy in
`NativeSourceResidency::finish_frame_encode`
(`src/render_graph/pipeline/scene_native_output.cpp`) — the graph's final
composite should target the slot's encode surface directly so no
source→encode-surface copy exists.

Landed (CODE DONE):

- **GraphExecutor terminal surface binding live.** The `GraphExecutor` designates
  the slot's `native_video_encode_surface` as the terminal output's physical
  surface BEFORE the terminal node executes. The terminal node renders directly
  there without any intermediate allocation or blit.
- **Degraded D2D copy lane rejected.** `scene_native_output.cpp` no longer
  falls back to a degraded D2D copy when source != encode surface: it fails
  closed, certifying Gate 6 (`gpu_surface_copy_frames == 0`) by architecture.
- **Equivalence gate tool.** `tools/verify_z3_equivalence.sh` is available for
  direct GPU equivalence certification (`gpu_surface_copy_frames == 0`).

**Status:** `Z3 CODE DONE — CERTIFICATION PENDING GPU`. No additional Z3 code
is needed; remaining step is executing `tools/verify_z3_equivalence.sh` against
the GPU environment.

## Z4 — Certified effect set on direct NV12 via capability matrix (PARTIAL — matrix landed)

Target: gates 3/4 → 0 for the certified corpus. The mechanism and the
effect/compositor capability matrix are landed:
`composite_direct_nv12_batch`, the DirectYuv lane, and
`resolve_direct_yuv_support()` provide `Require → hard fail` versus
`Auto → explicit FullGraph fallback` (never silent). Remaining is evidence:
the certified corpus (scene + expected gates) must run in CI without a content
pack. Non-certified scenes keep the honest FullGraph lane with counted
conversions — never counter-gaming.

The canonical prepared MTSDF text lowering is now admitted by the matrix only
when the resolver has validated a `TextRunShape` and sets
`LayerYuvQuery::prepared_mtsdf`. Generic GPU-resident text remains an explicit
FullGraph fallback. This makes the existing DirectYUV MTSDF kernel reachable
without weakening the fail-closed contract; visual Vulkan↔CUDA equivalence
and same-SHA GPU certification remain required evidence.

## Z5 — Absorb DirectYuvExecutor into the normal execution path (LAND)

`DirectYuvExecutor` (`src/media/video/direct_yuv/`) was absorbed into
`DirectYuvProgram` + `pipe_export_direct_yuv.cpp` (commit `6876c242b`),
migrating its semantics into the canonical CLI slot/writer pipeline and
deleting the duplicate executor class files (`direct_yuv_executor.hpp/.cpp`).
The canonical bounded SPSC queue (`session.queue`) and `NativeAvEncoder` handle
direct YUV frames synchronously with unified progress, cancellation, and metrics.

## Z6 — Per-slot async overlap and depth tuning (TELEMETRY LANDED, SWEEP OPEN)

The queue depth is already bounded (`kCudaEncodeRingSlots = 6` encoder-side,
`GpuSlotPool::kDefaultCapacity = 3` render-side, plus `io_queue`). Depth must
be tuned from measured `slot_reuse_wait_ms / decode_wait_ms / compose_wait_ms /
encode_submit_wait_ms / encoder_in_flight` (some already exported in the timing
sidecar). No new ring/scheduler authority.

The existing queue and slot authorities now also publish
`job.pipeline.queue_depth_average`, `queue_depth_peak`,
`render_slots_in_flight_peak`, and `encode_frames_in_flight_peak`. A same-SHA
B00 smoke validates the fields; see
[`z6-pipeline-depth-a16c86de2`](../evidence/z6-pipeline-depth-a16c86de2.md).
The controlled depth sweep and full-corpus comparison remain open.

## Z7 — Source gates + certification (PARTIAL, 2026-09-07)

Landed in this session:
- gate 7 instrumentation (F3) — canary, no behavior change;
- certification tool default scene fixed (F4) so the gate is runnable on main.

Remaining Z7:
- CUDA-lane upload counters or two-window certification (F5/F7);
- nightlies/CI zero-copy job with the registered scene on a GPU host;
- keep all old-error counters as regression canaries after paths are removed.

The certified timing sidecar now also reports
`job.full_file_reread_count`. This keeps mux checksum backpatch fallbacks
explicit in the same artifact as the zero-copy counters; a non-zero value is
not treated as incremental hashing.

Hash finalization accounting is now complete at the mux authority:
`job.hash` reports output writes, seeks, overwritten bytes, invalidations, and
final reread bytes. The B00 smoke confirms the expected MP4 backpatch path
(`2` seeks, `4` overwritten bytes, `1` invalidation, `30416` reread bytes),
without changing the authoritative checksum. See
[`hash-finalization-a16c86de2`](../evidence/hash-finalization-a16c86de2.md).

## Wave closure update — 2026-09-11

The CLI render-plan path now invokes `compiler::compile()` exactly once. The
video exporter receives a temporary scene-evaluation compatibility view built
from that immutable `CompiledProgram`; the compatibility branch does not call
`compile_composition()`. A declarative architecture contract prevents the CLI
preparation layer from reintroducing the second compile.

FullGraph native slots now own one exportable Vulkan backing. The source and
encode roles alias the same handle, the graph terminal is bound before node
execution, and the session destructor deduplicates release. FFmpeg CUDA
hwframes are materialized during native-surface warm-up, then reused by the
bounded encoder ring.

The Vulkan tiled text bridge was then aligned with the canonical
`runtime::TextRunDynamic` ABI (`3e4ed832d`). The shader and host originally used
112-byte records including selector/tracking/effect bindings. Later per-glyph
rotation and fill-color support extended the current tile ABI to 128 bytes;
the canary and `tests/test_shader_layout_contract.py` pin that current size and
compare both tile shaders to the C++ record and compiled SPIR-V stride. In the
2026-09-25 `linux-video-test` lane, the layout contract ran the compiled stride
checks, and the text canary, shader ABI consistency suite, and render-graph
suite passed. These local checks do not replace same-SHA Release/video corpus
certification.

The static certification is green (`185/185` architecture rules and the
steady-state allocation gate at `0.0` allocations/frame). RenderingGen
classifier/argument and processor contract tests are green. The reduced
runtime Z3 video certification was attempted on 2026-09-11 but this host's
capability probe reported no NVENC device; it remains pending on an NVENC
certification host and is not treated as a successful GPU result.

---

## Demolition debt schedule

1. **FullGraph native per-frame D2D copy** (`finish_frame_encode` →
   `copy_surface_to_cuda_encoder` source→encode surface).
   - Owner: rendering-core/video maintainers.
   - Reason: FullGraph output is a transient RGBA surface copied into the slot
     encode surface before the CUDA RGBA→NV12 handoff.
   - Exit condition: `gpu_surface_copy_frames == 0` on the certified run with
     identical output (Z3 equivalence gate green).
   - Equivalence gate: `tools/verify_z3_equivalence.sh` (reference vs
     candidate: rendered-frame RGBA parity + candidate gate 6 == 0).
   - Removal scope: `copy_surface_to_video_encode` per-frame callsite in
     `scene_native_output.cpp` (alias-binding no-copy branch landed 2026-09-07;
     delete the degraded-lane blit + `finish_reused_native_frame` blit once the
     executor binds the frame output to the slot encode surface — spec in the
     Z3 section); the encode-surface alias binding replaces it.
   - Status: `ACTIVE` (seam + gate landed; pixel binding pending GPU).
2. **FFmpeg NV12 pool as second per-slot allocation** (per-frame
   `av_hwframe_get_buffer` in the native encoder).
   - Owner: media/video maintainers.
   - Reason: NVENC consumption goes through FFmpeg hwframes rather than the
     slot's exportable backing.
   - Exit condition: encoder consumes the slot backing (register count ≈
     output slots, ≪ frames) with identical output.
   - Equivalence gate: native encoder contract tests + output hash parity.
   - Removal scope: per-frame hwframe acquire path / pool copy semantics.
   - Status: `ACTIVE`.
3. **`DirectYuvExecutor` second frame loop** — see Z5; removal only after the
   absorbed path certifies.
   - Status: `ACTIVE`.

## Wave code delta (2026-09-07, second session)

Code landed in this session (no render-behavior change on the certified
FullGraph lane; the Z3/Z4/Z5 GPU-equivalence deltas below stay gated on
GPU iteration per the plan's own ordering):

- **Z1 — typed encoder feed capability contract.**
  `include/chronon3d/media/video/encoder_capability.hpp`
  (`EncoderFeed`, `EncoderCapabilitySet`, `resolve_encoder_capabilities(plan)`)
  maps the final plan onto exactly one feed kind. The single encoder
authority enforces it: `NativeAvEncoder` now fails closed when a frame feed
crosses lanes (`write_frame` on a DirectYUV session, `write_direct_yuv` on a
non-DirectYUV session, `write_native_surface` on a DirectYUV session), and
`setup_pipe_export_session` logs the resolved feed set per session. Tests:
`tests/video/test_encoder_capability.cpp` (chronon3d_zero_copy_capability_tests).
- **Z4 — effect/compositor capability matrix (declared authority).**
  `include/chronon3d/media/video/composition_capability.hpp`
  (`DirectYuvEffectClass`, `classify_direct_yuv`, `resolve_direct_yuv_support`
  with `Auto → explicit fallback` vs `Require → hard fail`) states the
  certified effect set behind the DirectYuv lane. The runtime enforcement
  sites (`DirectYuvProgram::prepare`/`execute` fail-closed checks) mirror
  this matrix; the certified *corpus* runnable in CI without a content pack
  and the lane-level pre-screening consumer remain the GPU-step follow-up.
  Tests: `tests/video/test_composition_capability.cpp` (same suite).
- **Z7 — CUDA-lane gate-1 instrumentation (F5) + two-window semantics (F7).**
  `cuda_host_upload_bytes` counter added (VIDEO counter domain); the single
  CUDA upload authority (`VideoDeviceRuntime::get_or_upload_image`) counts
  host→device bytes on `VideoDeviceRuntime` (device-cumulative) and mirrors
  them into the job `RenderCounters`; the timing sidecar exports
  `job.gpu.cuda_host_upload_bytes`; `tools/verify_zero_copy_end_to_end.sh`
  reports it informationally; `docs/ZERO_COPY_GATES.md` documents the
  Vulkan/CUDA lane split and setup vs steady-state windows. All old-error
  counters remain as regression canaries (zero-forever).

### Session closeout fixes (same wave, code only)

Two coherence fixes landed on the same uncommitted delta before this ticket
was recorded:

1. **`ipc_render_job` executes the already-normalized job** (Phase 9 parity
   refactor): verified the chain `ipc_render_job →
   execute_normalized_render_job (command_render_plan_internal.inc) →
   execute_render_plan` is coherent — the daemon-side `NormalizedRenderJob`
   reaches `execute_render_plan` with no JSON parse and no second
   normalization, exactly like `run_render_plan_file` after its single
   normalize.
2. **Encoder open deferred until after the DirectYuv candidate decision**
   (`setup_pipe_export_session`): the Z1 typed-feed guards make
   `direct_yuv_mode` a session-fixed capability, so an Auto-rejected DirectYuv
   candidate that falls back to FullGraph must open the encoder AFTER the
   fallback decision (feed = FullGraph), never with the stale DirectYuv flag
   that would refuse every `write_frame`/`write_native_surface` on the
   fallback lane. `track_pipe_encoder_process` stays after `open()` so the
   pipe-encoder PID remains observable. `REQUIRE_DIRECT_YUV` rejection now
   returns with the encoder never opened (no empty mux artifact).

## Verification snapshot (2026-09-07)

- Z0 census: complete; no render behavior changed (two measurement/tool
  fixes only).
- Z1 contract + Z4 matrix + Z7 CUDA-lane counter: code landed, CPU/unit
  contract tests added; no render behavior change on the certified FullGraph
  lane (guards fail closed only on impossible cross-lane feeds).
- Certification run: pending the CUDA/Vulkan build of this SHA
  (`build/chronon/linux-video-fast-dev`) on the GPU host; expected current
  result per F1/F2: gates 4 and 6 non-zero, `surface_handoff_path = vulkan_copy`
  → `ZERO_COPY_FAIL` until Z3/Z4 land. Not marked green.

## Same-SHA FullGraph smoke (2026-09-11)

`a16c86de2` now has a 120-frame RTX A4000/NVENC smoke record in
[`gpu-b00-native-smoke-a16c86de2`](../evidence/gpu-b00-native-smoke-a16c86de2.md):
`gpu_surface_copy_frames=0`, CPU/GPU readback `=0`, host/CUDA upload `=0`,
and direct surface handoff. This certifies the measured FullGraph native lane
only. `rgba_to_nv12_frames=120` remains expected there; DirectYUV corpus
certification and the single-backing Z3/Z5 endgame remain open.
