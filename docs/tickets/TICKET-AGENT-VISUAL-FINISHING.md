# Agent Visual Finishing — CPU-only parity work

## Update 2026-10-03 — runtime export, scopes, OCIO, and subject mask probes

- Real 64×64 CPU/native exports were decoded with `ffprobe`: PQ H.264 High 10,
  PQ H.265 Main 10, and HLG H.265 Main 10 all carry `yuv420p10le`, limited
  range, BT.2020 primaries, BT.2020-NCL matrix, and the requested transfer.
  Requested ST 2086 mastering and Content Light metadata are present in the
  encoded streams. A PQ scopes pass measured the decoded signal at 215.14 nits
  peak against the 203-nit reference-white probe (lossy CRF output); HLG scopes
  also completed against the decoded export.
- The probe exposed that mastering/CLL side data was attached after encoder
  initialization, too late for the codecs to configure static HDR signaling.
  It is now configured on the codec context before `avcodec_open2`.
- An OCIO Studio display/view PNG probe preserved transparent pixels at alpha
  0 and the half-alpha red test at alpha 128, with the expected transformed
  color (205, 0, 6).
- The local ONNX provider generated a matte from a 640×360 real video frame and
  passed both foreground/uncertainty gates. The four-frame, 80×45 propagation
  probe published all masks with a passing report. A higher-resolution
  propagation probe failed its confidence gate and correctly published no
  outputs. Matte quality on project footage and reviewed edges remain open.
- The focused encoder tests found one stale profile assertion after H.264 High
  became explicit; it now checks High. The rebuilt encoder test selection
  passes: 32 test cases and 1,607 assertions.
- `git diff --check` passes. Full project verification and matte quality on
  representative Chronon projects remain open.

## Update 2026-10-03 — semantic matte provider and HDR encoder correction

- Added `tools/semantic_subject_mask.py`, an optional local ONNX/rembg provider
  that publishes a source-SHA-bound grayscale matte only after foreground and
  uncertainty gates pass. It complements `mask-segment`'s scribble-guided
  initializer and feeds the existing `mask-propagate` RenderPlan workflow.
  Provider runtime verification on real video passed; representative project
  footage quality and reviewed-edge acceptance remain open.
- A generated PQ probe exposed that software encoder resolution mapped H.265
  to `libx264`, and that 10-bit H.264 was opened with the 8-bit High profile.
  H.265 now maps to `libx265`; PQ/HLG H.264 requests select High 10. RenderPlan
  codec selection now survives the normalizer's implicit `auto` default.
  End-to-end encoded signal and mux metadata were verified in CPU/native probes.

## Update 2026-10-03 — bind propagated mattes to source bytes

- `mask-propagate` report frames now include `source_sha256`. Direct video
  inputs bind each presentation-order frame index to the hash of the exact
  video file; image-sequence inputs hash each corresponding frame file. Video
  reports also carry exact integer PTS and rational time base from the decoder's
  source sample table. The agent workflow documents this association. Runtime
  verification remains open; no tests or media renders were run.

## Update 2026-10-03 — publish propagated mattes only after acceptance

- `mask-propagate` now stages mask and confidence PNGs in its temporary spool
  while it evaluates the whole-clip tracking gates. It publishes the sequence
  only after every required frame passes and records `outputs_published` in an
  atomically published report. A rejected sequence is no longer left at the
  requested output paths as if it were accepted. The CLI build and runtime
  gate behavior remain to be verified; no tests or media renders were run.

## Update 2026-10-03 — alpha-weight the optical-flow image pyramid

- Temporal tracking now excludes fully transparent RGB from base luminance and
  computes each downsampled pyramid level from alpha-weighted visible
  luminance. Arbitrary RGB under zero alpha can no longer steer coarse-level
  flow and then contaminate a propagated matte. The tracking source compiles
  with the CLI target; temporal quality on RGBA footage remains unverified.
- No tests or media renders were run.

## Update 2026-10-03 — reject chroma mattes before publishing

- `mask-key` previously wrote its grayscale matte before evaluating the
  foreground-fraction gate, so a rejected result still existed at the requested
  output path. It now publishes the PNG only when the gate passes and reports
  `output_written` with the gate result. The agent workflow documents this
  fail-closed behavior. Build and pixel-level runtime evidence remain pending;
  no tests or renders were run.

## Update 2026-10-03 — workflow contract synchronized with OCIO output wiring

- The agent workflow now documents the already implemented RenderPlan
  `output_display`/`output_view` path: CPU video and PNG output apply the view,
  EXR rejects it, and GPU-required output is rejected. Older status notes below
  saying display/view output was unwired are historical and superseded.
- Remaining color work is runtime evidence: compare encoded pixels and stream
  metadata against declared transfer/primaries, especially for HDR. No test or
  media render was run; the CPU/native-video/OCIO CLI target is already built.

## Update 2026-10-03 — fail closed on corrupt authored mask anchors

- `TemporalMaskPropagator::propagate_anchored_sequence` now rejects a
  non-finite alpha sample in an authored correction matte. Previously the
  sample became transparent with zero confidence while anchor frames were
  exempt from confidence gates, silently changing the correction.
- The CPU/native-video/OCIO CLI target compiles and links after this change;
  no tests or media renders were run.
- Dense pyramidal flow now builds a coverage pyramid from frame alpha and
  weights each Lucas–Kanade window by visible source/target coverage. Fully
  transparent pixels no longer contribute arbitrary RGB to the motion model;
  partially covered edge pixels contribute proportionally. This path compiles,
  but temporal quality still needs real RGBA footage verification.
- The guided boundary refinement now computes its local statistics with
  source-frame alpha weights and leaves fully transparent pixels outside the
  refined matte. The target recompiles and links; pixel quality remains
  unverified on real RGBA footage.
- `mask-propagate` confidence means are now weighted by each output frame's
  visible alpha coverage. Transparent canvas area no longer depresses the
  tracking gate or foreground-confidence mean; reports expose the visible
  coverage used as each denominator. The CLI compiles and links; media gate
  behavior remains unverified.

## Update 2026-10-03 — OCIO display/view output

- RenderPlan `color_management.output_display` and `output_view` now form a
  paired output declaration. The pair participates in the plan fingerprint,
  resolves the config inside the mounted asset root, and reaches video encoder
  options and still/sequence image writing.
- CPU native and pipe encoders construct the OCIO display/view processor once
  per encoder session, composite transparent pixels over black in linear
  premultiplied space, apply the display transform, then skip the encoder's
  second transfer curve. The PQ/HLG YUV path has an explicit already-transformed
  input marker so it applies only the matrix/range conversion afterward.
- PNG still/sequence output writes view-encoded RGB directly (without another
  sRGB OETF); the RenderPlan must declare `output.color_space: "srgb"`. EXR
  rejects display/view transforms and remains scene-linear.
- The CPU/native-video/OCIO CLI target compiles and links; no tests or media
  renders were run. HDR transfer, primaries, alpha edges, and encoded metadata
  still need checking against actual output. `inspect --plan` and receipt-bound
  `scopes` reports expose the selected display/view.

## Update 2026-10-03 — nested composition cache semantics

- `PrecompNode` now declares a frame-variant cache policy. Its output evaluates
  the nested composition at the current frame minus the precomp start offset,
  so it must not be classified as a frame-invariant asset by graph execution
  or downstream cache-key propagation.
- The precomp cache key now includes its composition name, start offset, and
  duration. This keeps diagnostic and dependency identity aligned when two
  nodes reference the same composition with different timing windows.
- The CPU/native-video/OCIO CLI target compiles and links after this change;
  no tests or media renders were run. Display/view and HDR output pixels still
  need runtime verification, as does model matte quality on project footage.
  Cryptographic per-frame source association remains open as listed below.

## Status

OPEN — software-side work for agent-authored motion graphics and compositing.
This ticket excludes Vulkan/GPU effect parity and human-facing NLE features.

## Goal

Give an agent a deterministic, inspectable software workflow for producing and
finishing a video: author declared composites, isolate and track subjects,
grade in a documented color space, inspect scopes, and export an output whose
pixel values and media metadata agree. Existing render-plan and render-graph
authorities remain canonical; do not add a parallel scene or effect system.

## Current evidence and work

- Scene masks already support ordered Add/Subtract/Intersect/Difference,
  inversion, feather, expansion, opacity, and animated parameters. Track matte
  supports Alpha, Alpha Inverted, Luma and Luma Inverted. Precompositions are
  supported. Remaining work is software-path coverage and agent-facing
  composition contracts, not adding these primitives from scratch.
- RenderPlan track-matte resolution now fails closed when the referenced source
  is missing or inactive on a frame where the target is active. The software
  graph builder reports the matte layer id and frame instead of silently
  omitting the matte node and rendering an unmasked target.
- RenderPlan V3's first mask now honors `mode: "subtract"` against the implicit
  full layer mask, including rasterization outside the shape bounds. Previously
  the decoder accepted the mode but the plan compiler only copied its geometry
  and silently treated it as Add. The selected initial mode is part of the
  render-graph mask cache hash; initial Add remains the default for legacy
  builder masks. `chronon3d inspect --plan` successfully decoded and compiled a
  V3 plan whose first mask uses Subtract. Pixel-level compositing evidence is
  still open because no test or render corpus was run.
- `color::make_waveform` and `color::make_vectorscope` provide CPU scope data
  from Chronon framebuffer pixels, including premultiplied alpha and padded
  row stride. `chronon3d scopes <rendered-video-or-still...>` emits sparse,
  machine-readable waveform/vectorscope JSON for 8-bit PNG/JPEG interpreted
  as sRGB by default, Rec.709 (`--input-transfer rec709`), Display P3/D65
  (`displayp3`) or linear BT.709 (`linearsrgb`). Rec.709 code values are
  re-decoded after the image loader's sRGB conversion, and P3 primaries are
  converted to BT.709 for scope calculations. When
  `CHRONON3D_ENABLE_EXR` is enabled, RGBA scanline EXR is read
  directly into linear-float framebuffer storage. The EXR reader now interprets
  tagged chromaticities and converts RGB to linear BT.709 before scope analysis,
  applying Bradford chromatic adaptation when the source white differs from
  D65. Untagged files retain the OpenEXR BT.709 default. Invalid or singular
  chromaticities fail closed. The EXR writer tags both FLOAT and HALF outputs
  with BT.709/D65 chromaticities while preserving linear scene headroom. It
  writes standard OpenEXR `whiteLuminance` from the RenderPlan reference
  white. `scopes` now reports that attribute for EXR and, when given the
  RenderPlan receipt, requires it to match the declared reference white within
  `max(0.01 nit, 0.01%)`; absent/mismatched metadata fails the gate.
- Render-plan EXR image assets now use the float EXR reader and enter ImageCache
  as premultiplied linear BT.709 RGBA floats. The cache preserves scene headroom
  in its software framebuffer and does not create an 8-bit Blend2D copy for
  those assets; ordinary PNG/JPEG decoding keeps the existing 8-bit sRGB path.
  Standard and tiled software image drawing both accept the framebuffer-only
  cache entry, so tiled EXRs no longer disappear when the optional Blend2D copy
  is absent.
- RenderPlan byte-backed image layers now declare `input_color_space` as
  `srgb` (default), `linearsrgb`, `rec709`, or `displayp3` (sRGB transfer,
  P3/D65 primaries). ImageCache applies transfer decoding and P3-to-BT.709
  conversion before premultiplication,
  and the decode option participates in the canonical cache key. The plan
  fingerprint includes the declaration so a transfer change cannot reuse the
  prior image/render identity.
  Semantic-script image backgrounds and `image_overlay` events preserve the
  same declaration into RenderPlan; text events reject it. `inspect --plan`
  reports the resolved image transfer in text and JSON.
  `--signal-min/--signal-max` expose scene-linear headroom in the waveform;
  Rec.709 inputs use the Rec.709 OETF and sRGB inputs use the sRGB OETF.
  PQ/HLG video scopes
  convert the linear BT.709 framebuffer back to BT.2020 primaries and apply
  ST 2084 or ARIB STD-B67 before analysis; BT.2020 luma/chroma coefficients
  are used. It supports luma, RGB parade and alpha coverage waveforms plus
  vectorscope. Alpha mode includes fully transparent pixels and bypasses RGB
  transfer encoding so soft matte coverage remains directly readable. Each
  frame also reports fully transparent, partial and fully opaque counts and
  mean alpha for downstream agent gates. It
  analyzes the decoded image signal and reports
  full-float signal bounds, luma coefficients, YCbCr matrix and its
  encoding. With `--render-receipt`, it reports RenderPlan fingerprints,
  declared output color/reference white and per-image frame indices; it checks
  the requested frame range, dimensions and SDR transfer against the receipt.
  For a video file it also checks SHA-256 against the rendered artifact and
  walks the decoder's indexed presentation samples; `--video-frame-step`
  controls temporal sampling. Rendered still sequences now record and verify
  each image's frame index, byte count and SHA-256. Still frames extracted
  separately from a video remain caller-asserted because the video receipt
  does not store per-frame hashes.
  Scope bin
  allocations are capped at 16,777,216 bins per
  analyzer call (128 MiB of counters) and reject larger requests.
- The built-in Rec.709 output conversion now uses the BT.709 OETF instead of
  the sRGB OETF. Existing assertions that encoded both as identical were
  updated to the BT.709 values.
- CPU video frame conversion now carries a distinct Rec.709 OETF flag through
  packed and swscale staging and includes it in both converted-frame cache
  keys. It also converts the linear framebuffer into planar BT.2020 YUV420 10-bit
  for PQ/HLG native CPU export. The encoder bitstream contract marks sRGB as
  IEC 61966-2-1 and Rec.709 as BT.709 transfer.
- The float RGB output-transform API now decodes and encodes sRGB, Rec.709,
  BT.2020 linear, PQ and HLG, including BT.709↔BT.2020 primary conversion.
  PQ uses an explicit reference-white scale; ACEScg fails closed and remains
  OCIO-only. The RGB8 API rejects HDR/wide-gamut targets instead of clipping
  them under misleading metadata.
- RenderPlan V3 already provides an agent-callable grading stack: adjustment
  layers accept ordered catalog effects and animated effect parameters. The
  software effect catalog includes Exposure, Levels, Curves and 3D LUT, so
  agents can declare and order these operations without constructing raw graph
  nodes. The basic declarative grade-entry gap is closed. Remaining grading
  gaps are a declared project/input working-space contract and whole-stack
  OCIO. The catalog now includes an animatable Lift/Gamma/Gain adjustment in
  linear RGB in addition to normalized Temperature and Tint controls. Lift is
  a signed additive offset, gamma is a signed power curve, and gain is a
  linear multiplier; the operation preserves highlight headroom. Temperature
  and tint remain creative normalized adjustments, not measured Kelvin
  white-balance calibration. The software pipeline preserves float headroom
  for exposure, levels, curves, temperature/tint and Lift/Gamma/Gain; LUT
  sampling clamps coordinates to its
  declared domain, so HDR looks need an LUT domain that includes the signal.
  OCIO named scene looks can be baked to bounded-size `.cube` LUT assets in
  Chronon's linear BT.709 working space, then inserted at an explicit point in
  that same ordered RenderPlan stack. The OCIO LookTransform performs its
  configured process-space conversion and returns to the declared working
  space. Project input transforms and display/view output transforms remain
  open.
  Example V3 adjustment entry:

  ```json
  {
    "id": "grade",
    "type": "adjustment",
    "effects": [
      {"type": "catalog", "effect_id": "color.exposure",
       "params": [{"param": "stops", "value": 0.35}]},
      {"type": "catalog", "effect_id": "color.temperature_tint",
       "params": [{"param": "temperature", "value": 0.12},
                  {"param": "tint", "value": -0.04}]},
      {"type": "catalog", "effect_id": "color.lift_gamma_gain",
       "params": [{"param": "lift", "value": 0.01},
                  {"param": "gamma", "value": 1.05},
                  {"param": "gain", "value": 1.02}]}
    ]
  }
  ```
- The documented adjustment plan was accepted by `chronon3d inspect --plan`
  with both Temperature/Tint and Lift/Gamma/Gain parameters. That inspection
  exposed that adjustment and several other V3 layer types were reported as
  `unknown`; `inspect` now names Shape, Particles, Light, Adjustment and Mesh
  explicitly as well.
- The optional OCIO transform now has a CPU framebuffer operation as well as
  its single-triplet API. It unpremultiplies visible pixels, applies the
  declared OCIO input-to-output transform, then restores premultiplication
  while preserving alpha. It stages the whole visible frame and rejects
  invalid alpha/RGB or non-finite transform results without partially
  mutating the source frame. This makes full-frame CPU transforms possible, but
  RenderPlan now optionally declares `color_management.ocio_config` and a
  named `working_space`; byte-backed image layers can set `ocio_input_space`.
  The compiler resolves the config under the mounted asset root, and ImageCache
  applies the source-to-working-space transform before premultiplication.
  The config file is included in the prepared asset manifest, so changing its
  contents changes the job fingerprint even when its logical path stays fixed;
  image decode options also separate the cache entries. `inspect --plan`
  reports the project working space and each image input space. OCIO-disabled builds fail
  with a diagnostic; EXR rejects an extra OCIO input transform because it
  already decodes to linear BT.709. The working space is contractually required
  to be scene-linear and BT.709-compatible. Image and video layers can declare
  `ocio_input_space`; `color_management.video_input_space` now sets a project
  default for all video layers, with the layer field overriding it. The CPU
  video decoder applies the effective input space to transfer-coded RGB
  before transfer decoding, bypasses GPU-native import for that source, and
  disables DirectYUV eligibility. OCIO processors are reused by source-space
  tuple and config file metadata; transformed framebuffers bypass the default
  decode cache and prefetch queues so one input-space interpretation cannot
  contaminate another. `inspect --plan` shows the video input space. CPU-only
  and combined FFmpeg+OCIO source/object compilation succeeded for the decoder,
  RenderPlan compiler, graph source pass, image cache, manifest and inspection
  CLI; no media render was run. Display/view output transforms remain open.
  Creative scene looks can enter ordered
  adjustment stacks through the separate OCIO-to-LUT bake command below.
  The `linux-ocio-certification` Release profile configured successfully and
  compiled and linked the CLI with `CHRONON3D_ENABLE_OCIO=ON`; CLI help exposes
  `scopes` and `mask-propagate`. This verifies the optional integration builds,
  not the pixel results or project-level transform scheduling. The subsequent
  atomic-framebuffer update also compiled and linked in that profile; it has
  not been exercised against reference images.
- Added the optional `ocio-bake-lut` CLI command. It bakes either a configured
  OCIO LookTransform from and back to a named working space or an explicit
  named color-space conversion into a `.cube` LUT, with a caller-declared
  domain and 2..48 cube size. The existing V3 effect stacks can place looks
  between ordered grades or convert a known source layer before compositing.
  Source spaces remain caller-declared; the plan does not detect them. The command and
  LookTransform compiled and linked with OCIO 2.5.1 enabled, and
  `ocio-bake-lut --help` exposes the options. A real config bake and LUT render
  have not been run.
- RenderPlan v2 now declares `output.color_space` and
  `output.reference_white_nits`. The render-plan decoder carries them into
  native CPU video export unless a CLI override is supplied; semantic-script
  input/output preserves both fields. Render job validation and fingerprints
  include explicit overrides, and `inspect --plan` reports the resolved
  output color contract. Defaults remain sRGB and 100 nits. This closes the
  video output declaration portion. The render-plan CLI now rejects still or
  sequence outputs whose declared transfer disagrees with the actual writer
  (`.png` sRGB, `.exr` linear BT.709). Input primaries/range/mastering metadata and
  whole-stack OCIO transforms are still open.
  The decoder also validates the color-space enum (`srgb`, `rec709`,
  `linearsrgb`, `pq`, `hlg`) and rejects non-finite, non-positive or over
  10000-nit reference-white values before render scheduling.
- Still and sequence EXR output now carries OpenEXR's standard
  `whiteLuminance` attribute, defining the absolute luminance in nits of
  linear RGB `(1,1,1)` from `output.reference_white_nits`. The value is threaded
  through the canonical render job to both FLOAT and HALF EXR writers; direct
  image-writer callers leave it unspecified by default. This is the EXR
  reference-white scale, not HDR10 mastering-display peak/primaries or MaxCLL.
- The software video decoder now requests RGBA64 staging, decodes the declared
  Rec.709, sRGB, linear, gamma 2.2, PQ or HLG transfer, and converts BT.2020
  linear primaries into the engine's BT.709 working primaries. It also accepts
  SMPTE 170M and BT.470BG SD primaries plus Display P3/D65, converting those
  linear RGB samples to BT.709 after transfer decoding. When primaries are
  unspecified, the decoder derives the SD primary set from an explicit
  BT.470BG or SMPTE 170M YUV matrix. The conversion matrices are derived from
  the standards' D65 primary coordinates. DCI-P3's non-D65 white remains
  unsupported in video decode. The native software decoder now fails closed on
  unsupported YUV matrices instead of silently interpreting them as BT.709;
  supported inputs are BT.709, BT.470BG, SMPTE 170M, BT.2020 NCL, RGB, or an
  unspecified matrix using the documented BT.709 fallback. BT.2020 constant
  luminance is explicitly rejected. Unspecified
  transfer metadata retains the documented BT.709 SDR fallback.
- RenderPlan V2/V3 can declare optional ST 2086 mastering-display chromaticities
  and luminance plus CTA-861.3 MaxCLL/MaxFALL. The values enter the software
  encoder as per-frame FFmpeg mastering/content-light side data and are copied
  into the output receipt as the requested contract. The receipt now checks
  ffprobe stream side data against that declaration (0.000001 xy and 0.00001
  nit tolerances, matching six-decimal side-data precision); a missing/mismatched
  value fails copy eligibility. Explicit
  metadata is rejected for pipe and hardware routes. Runtime mux preservation
  remains to be demonstrated on actual HDR media.
- The `linux-video-cpu` native-FFmpeg CLI target compiled and linked after
  adding this metadata path. The installed FFmpeg did not export its convenience
  side-data allocation helpers, so the encoder now allocates typed AVFrame side
  data with `av_frame_new_side_data`. The native CLI and receipt verifier
  compiled and linked; no HDR media render or tests were run.
- `chronon3d scopes --render-receipt` now rejects an HDR mastering declaration
  unless the receipt's bitstream metadata gate passed, and includes the declared
  values and gate result in its scope JSON. This prevents a scope report from
  appearing successful when the mastering contract failed. The CPU/video CLI
  compiled and linked with the gate; no media render or tests were run.
- The native CPU encoder's metadata mapper represents PQ as SMPTE ST 2084,
  HLG as ARIB STD-B67, and HDR/wide-gamut signals with BT.2020 matrix and
  primaries. Native software encoding selects planar YUV420 10-bit for PQ and
  HLG and converts the linear BT.709 framebuffer to BT.2020 limited-range
  output using the requested reference white (`--reference-white-nits`,
  default 100). It fails closed if the selected software codec lacks that pixel
  format. GPU encoding and pipe-based HDR remain unsupported. Linear BT.2020
  video output and ACEScg video output remain unsupported. Y and Cb/Cr samples
  are clamped to nominal limited-range codes 64..940 and 64..960 respectively.
- `TemporalMaskPropagator` now propagates an authored/segmented soft mask
  across CPU frame sequences with coarse-to-fine dense Lucas–Kanade flow,
  forward/backward consistency, a bounded work budget and per-pixel confidence.
  Confidence history follows each propagated matte across frames; after a
  region loses confidence it cannot silently recover until an authored seed
  or correction anchor resets that tracking interval.
- `chronon3d mask-key` now derives a soft CPU chroma-key matte from a single
  PNG/JPEG frame and emits a JSON parameter receipt. It shares the exact
  chroma-distance, softness, clip, choke and garbage-matte coverage function
  with the software Keying effect; its output is directly usable as the seed
  for `mask-propagate`. Its receipt includes coverage/edge statistics and an
  optional foreground-fraction gate that rejects empty or nearly full-frame
  keys. Semantic segmentation for non-screen material remains external, and
  generated keys still need visual inspection/correction.
- `chronon3d mask-segment` now provides a CPU scribble-guided seed matte for
  non-keyed stills: normalized foreground/background points fit linear-BT.709
  regularized full-covariance Gaussian color models, followed by edge-weighted
  probability diffusion. The covariance retains cross-channel color
  relationships that the earlier independent-channel model discarded. It emits
  a soft PNG matte and a JSON report with foreground
  fraction and unseeded-pixel uncertainty, with configurable work and quality
  gates. This gives agents a native matte initializer when they can identify
  sparse subject/background points; it is not automatic semantic recognition
  and ambiguous boundaries still need correction anchors.
  Occluded, textureless and implausibly fast pixels are cleared and counted.
  Flow windows at image edges now use their available in-frame support instead
  of unconditionally rejecting an entire border band, avoiding progressive
  mask erosion at the frame boundary.
  This propagates a seed mask; it does not perform semantic subject discovery.
- `chronon3d mask-propagate <ordered-frame-images...>` exposes that CPU
  propagator to agents. It takes an authored seed matte and seed-frame index,
  propagates in both temporal directions, writes opaque grayscale PNG luma
  mattes, grayscale confidence maps and a JSON report with mean confidence and
  rejection counts. Repeated `--correction-mask FRAME=PATH` inputs add exact
  authored correction anchors; the interval between corrections is propagated
  in both directions and confidence-weighted per pixel. Intermediate forward
  mattes/confidences spill to temporary PNGs, keeping frame memory bounded;
  authored anchor mattes remain exact. `--input-transfer srgb|rec709` declares
  the extracted source frame transfer (sRGB default); the tracker converts
  Rec.709 frames to linear BT.709 before estimating motion, while seed and
  correction masks remain coverage data. The JSON report records this input
  transfer and tracking space. It accepts up to 10000 8-bit PNG/JPEG frames
  at up to 8 megapixels each, subject to a preflight estimate of total CPU
  tracking work (default budget: 100 billion work units; configurable with
  `--maximum-total-work-units`). The report records the estimate and budget;
  jobs over budget fail before propagation begins. The agent must extract the
  source frames and create/correct the seed matte. With `--plan-input`,
  `--plan-output`, `--target-layer`, `--matte-video`, and `--matte-layer-id`,
  the command now attaches a V3 `track_matte` relation and adds a source video
  layer that copies the target's placement, timing, and position/scale/rotation
  animation tracks (opacity and blur are not duplicated). With
  `--encode-matte-video` and native FFmpeg enabled, the command also encodes
  the matte sequence as CPU H.264 at the plan frame rate in the same
  invocation. Automatic encoding requires the input sequence to match an
  unlooped, non-retimed target duration; builds without native FFmpeg can still
  use a pre-encoded matte file. `--minimum-mean-confidence` provides an
  optional per-frame gate; failed reports prevent automatic encoding and plan
  attachment, while authored anchors are exempt. `--maximum-foreground-loss`
  separately gates lost source-matte samples so confident background pixels
  cannot conceal a lost subject. Before plan attachment, the
  encoded video is probed for H.264/YUV420P codec, exact plan frame rate,
  dimensions and BT.709 signal metadata, indexed
  for exact frame count, fully decoded on CPU and every coverage pixel is
  compared with the PNG source under a 0.02 absolute linear tolerance; the
  report records the measured errors. RenderPlan's resolve pass marks referenced
  matte sources as source-only, and the graph layer pass excludes them from the
  visible composite while still building the matte sub-pipeline. The generated
  plan patch is passed through the canonical RenderPlan decoder/validator
  before its temporary file is atomically published.
  `--video-input` now decodes one SDR or HDR video in presentation order,
  bounded by the same 10000-frame/8-megapixel limits. SDR requires explicit
  BT.709/sRGB transfer with BT.709 primaries/matrix; HDR requires PQ/HLG,
  BT.2020 primaries and the BT.2020 non-constant-luminance matrix; missing or
  mismatched metadata fails closed. Frames are staged as temporary linear
  BT.709 RGBA float records, avoiding an 8-bit image round-trip, with preflight
  tracking-work and 8 GiB default spool-byte budgets. PQ photometric confidence
  accounts for its 100-nit working units. HLG tracking uses decoded
  scene-linear values. This removes manual extraction; runtime quality evidence
  remains open.
  `--refine-radius` optionally applies a guided filter using linear-light frame
  luminance on propagated mattes; authored anchors stay exact, confidence is
  measured before refinement and the report labels refined frames. Automatic
  subject segmentation remains missing; agents must supply seed and correction
  mattes.
- A Linux Release CPU/video build with native FFmpeg on and Vulkan/CUDA off
  compiled and linked `chronon3d_cli`. It compiled the converter, 10-bit packer,
  native encoder, scopes and mask propagation commands. CLI help confirms
  `--color-output pq|hlg` and `--reference-white-nits` are exposed. System
  FFmpeg 62 reports deprecation warnings for the existing `AVCodec::pix_fmts`
  field. H.264 profile references were switched to the current `AV_PROFILE_*`
  constants so this configuration compiles. No tests were run. Runtime
  propagation, scope output and HDR stream output remain unverified.
- A separate Release build using OpenColorIO 2.5.1 compiled and linked the full
  CLI with OCIO enabled. `chronon3d_cli --help` exposed `scopes` and
  `mask-propagate`; no tests or media renders were run in that profile.
- Published a bounded agent procedure in
  `docs/AGENT_VISUAL_FINISHING_WORKFLOW.md`. Its command names, required
  options (including the required `--report` receipt gate) and receipt path
  were checked against the current CLI and receipt writer. The procedure makes
  the external matte-video encoding step
  and current manual segmentation inputs explicit; it has not been exercised
  on a real project/media package.

## Remaining work

- `mask-segment` now treats RGBA source alpha as coverage: invisible pixels are
  excluded from the color model, partial coverage weights the Gaussian fit,
  diffusion respects alpha edges, and output coverage preserves source alpha.
  Non-finite RGB at visible pixels and invalid alpha fail closed. The CLI build
  and alpha-aware output behavior remain unverified at runtime.
- `mask-segment` now computes and writes its gate report before publishing the
  matte. A failed foreground-fraction or uncertainty gate returns 2 with
  `matte_written: false`; successful output is followed by a report update to
  `matte_written: true`. This prevents agents from consuming a newly generated
  matte whose gate failed. Runtime file/gate behavior remains unverified.
- Guided matte-edge refinement now normalizes finite linear-light luminance
  before calculating second-order statistics and scales epsilon to preserve
  the input-space regularization. This prevents float overflow from high-range
  HDR guide pixels contaminating refined coverage. The media implementation
  and workflow documentation are updated; runtime behavior remains unverified.
- Frame-level scope quality gates now fail closed when a video scan uses
  `--video-frame-step > 1`; the JSON records incomplete source coverage at both
  gate and frame level. Sampled scope reports remain available for inspection.
  The command and documentation are updated; runtime media behavior remains
  unverified.
- The rasterized-mask cache key now includes `Mask::initial_mode`. Previously
  changing the primary mask's initial Add/Subtract/Intersect/Difference rule
  could return a matte rasterized under the previous rule. The change is
  implemented; runtime pixel behavior has not been exercised.
- The extended sRGB OETF no longer clamps values below zero or above one
  internally. Explicitly unclamped output now preserves gamut/headroom through
  transfer encoding; bounded output still clamps at the caller. The CPU/native-
  video/OCIO CLI build is being refreshed; transfer behavior has not been
  exercised with media.
- `OcioColorTransform` now exposes a CPU display/view processor factory using
  the source space, display, and view names from an OCIO config. This gives the
  output pipeline the right primitive without applying another OETF afterward;
  RenderPlan fields, output-pipeline wiring, and media verification remain
  open.
- Echo effect nodes now declare the history window implied by their enabled
  parameters (`echoes * time_offset_frames`, capped by the supported parameter
  limits). Segment planning can therefore request preroll instead of silently
  starting with empty trails. Software temporal history is scoped to a render
  session and cleared by the job reset path. The effect-stack header compiles
  in the CPU/native-video/OCIO profile; pixel-level Echo behavior and
  out-of-order frame execution remain unverified.
- RenderPlan `color_management.video_input_space` now applies one declared OCIO
  input space to video layers by default, with `layers[].ocio_input_space` as an
  override. The effective space appears in `inspect --plan`; the value is part
  of the content fingerprint and the mounted OCIO config remains in the asset
  manifest. V2 schema JSON parses and the decoder, plan compiler, fingerprint,
  and inspection CLI objects compile in the CPU/native-video/OCIO profile.
  OCIO display/view output transforms remain open.
- `chronon3d scopes` now supports optional per-frame quality gates for clipped
  waveform samples and fully opaque/transparent alpha coverage. It writes the
  report, records each measured fraction and pass/fail result, and exits 2 when
  a gate fails. The CLI object compiles in the CPU/native-video/OCIO profile;
  runtime gate behavior still needs media-based verification.
- PQ scope quality gates can bound decoded peak-pixel and per-frame-average
  luminance in nits. They require exactly one video input, a full presentation
  frame scan, and a measurement for every indexed frame; missing PQ data or
  frame skipping fails closed. The CLI object compiles in the CPU/native-video/
  OCIO profile; runtime HDR gate behavior remains unverified.
- `mask-propagate` now has an independent minimum mean-confidence gate over
  retained foreground matte pixels, alongside its whole-frame confidence and
  rejected-foreground gates. The report records the foreground sample count and
  confidence mean. This prevents high-confidence background pixels from
  obscuring an uncertain retained subject region; runtime tracking media still
  needs verification.
- `scopes` can now fail an export when the fraction of valid vectorscope samples
  outside the configured Cb/Cr range exceeds a supplied limit. Empty/transparent
  frames fail closed when this gate is requested. The CLI object compiles in the
  CPU/native-video/OCIO profile; media-based gate verification remains open.
- Text-backed masks are now materialized consistently for normal layers,
  clip-transition sub-pipelines, and displacement-map source sub-pipelines.
  Previously only normal layers attached their referenced text source nodes,
  so the other two paths could fail when building the mask node. The shared
  preparation path was compiled in the CPU/native-video/OCIO profile; a render
  using text masks in those sub-pipelines still needs runtime verification.
- The full-frame clear-skip optimization now checks `mask.enabled()` instead
  of only checking the primary mask type. This prevents additive/combined masks
  with a `None` primary type from being mistaken for opaque full-frame layers.
  The compiler object builds in the CPU/native-video/OCIO profile; pixel-level
  compositing evidence is still needed.
- Track-matte sub-pipelines now apply the matte source layer's own mask before
  its transform, then apply its ordered effects. Displacement-map inputs are
  materialized through the shared layer resolver with cycle detection.
  Text-source sub-pipelines deliberately stay unmasked and effect-free to avoid
  recursive mask sampling; matte sources that use text masks materialize the
  referenced text nodes. The matte builder and layer-pipeline objects compile
  in the CPU/native-video/OCIO profile; runtime alpha-boundary evidence remains
  open.
- Track-matte sources can now themselves use track mattes. The builder resolves
  the full source chain recursively, caches each sub-pipeline, and fails with a
  layer-specific cycle diagnostic for cyclic references. Matte builder and
  layer-pipeline objects compile; nested matte pixel behavior is not yet
  verified by render.
- The software Exposure, Levels and Curves processors now honor the effect
  execution clip rectangle, matching the other color processors. Previously
  those three modified pixels outside a partial update region. The processor
  object compiles; pixel-level clipped-region behavior remains unverified.
- A broader CPU effect audit fixed the same clip propagation gap in Blur,
  Tint, Fill, Radial Blur, Directional Blur and Stroke. Fake 3D Wave now
  computes its full sampling result but writes only the requested clip,
  preserving pixels outside a partial update. The color/motion processors and
  wave implementation compile, and the full CLI target relinks; pixel-level
  partial-region evidence remains open.
- The complete `chronon3d_cli` Release target rebuilt successfully in the
  `linux-video-ocio` profile after the current software color, matte,
  RenderPlan, scope and mask-propagation changes. This confirms cross-target
  compilation and linking with FFmpeg and OCIO enabled; it does not verify
  rendered pixels, tracking quality, or HDR bitstream behavior.
- RenderPlan relation validation now rejects Adjustment layers as track-matte
  or displacement-map sources during plan decoding, with path-specific
  diagnostics. These sources need raster pixels, which an adjustment layer
  does not emit. Precompositions remain valid sources. The decoder object
  compiles; no runtime render was used for this check.
- The RenderPlan acyclicity walk now includes displacement-map layer edges in
  the same dependency graph as parent, track-matte and text-mask references.
  Cross-reference cycles therefore fail during plan decoding instead of
  waiting for recursive graph materialization. The decoder object compiles;
  no test or render was run.
- Source inspection of the software `TrackMatteNode` confirms Alpha modes
  multiply all premultiplied target channels by matte alpha (or one minus
  alpha), while Luma modes derive BT.709 luma directly from premultiplied
  matte RGB, avoiding a second alpha multiplication. Matte and target pixels
  are aligned by canvas origins; outside-matte coverage is transparent for
  normal modes and fully covered for inverse modes. This is implementation
  evidence only: boundary, offset and nested-matte pixels still need rendered
  media verification.
- RenderPlan decoding now compares each target's active frame interval with
  its matte source interval, clipped to the composition duration. If the matte
  is inactive at any target frame, plan inspection reports the first uncovered
  frame before export starts. The decoder object compiles; no render was run.
- `chronon3d scopes --require-render-receipt` now makes receipt binding
  mandatory for acceptance runs while leaving exploratory source-image scope
  inspection available without the flag. The documented final-video and image
  sequence recipes use the flag. The scopes CLI object compiles; media gate
  behavior remains unverified.
- Scope JSON no longer reports an unconfigured quality gate as `pass`. With no
  thresholds, the overall state is `not_requested` and per-frame state is
  `not_evaluated`; supplied frame thresholds continue to report pass/fail.
  The scopes CLI object compiles; no media behavior was run.
- Waveform clipping fractions now use only valid signal samples: luma/RGB
  exclude transparent and non-finite pixels, while alpha coverage includes
  transparent pixels and excludes non-finite alpha. RGB parade counts each of
  its three channels. A requested clipping gate fails closed when no valid
  samples exist, and the report exposes valid and clipped sample counts. The
  CLI object compiles; media gate behavior is unverified.
- `scopes --max-non-finite-fraction` lets an agent reject NaN/Inf waveform
  pixels independently from clipping, and the final delivery recipe sets the
  limit to zero. The report records invalid counts and per-frame fractions.
  Compilation is verified; pixel-level gate behavior remains unverified.

1. Complete and enforce one project/input/output color contract. RenderPlan
   now declares output transfer and reference white, derives the supported
   primaries/matrix/range contract, carries optional HDR mastering metadata,
   and declares project/per-layer OCIO input spaces plus paired display/view
   output. Runtime proof must still compare known output pixels and muxed stream
   metadata against those declarations, including alpha edges and unsupported
   metadata rejection.
2. Exercise native CPU PQ/HLG export end-to-end on supported software codecs,
   including stream mastering/content-light metadata inspection and known HDR
   signals. OpenEXR writes reference-white luminance metadata, but runtime
   headroom evidence is open. Declared ST 2086 and CTA-861.3 metadata is attached
   and receipt-checked; runtime mux preservation is still to be demonstrated.
   Do not label an SDR encoded stream as PQ/HLG or silently clip HDR to 8-bit
   SDR.
3. Bind the `scopes` report to a RenderPlan frame receipt and its exact color
   transform. The command now decodes video with the software-only native
   decoder, walks indexed source samples in presentation order (including VFR
   PTS), reports source transfer/primaries, and accepts `--video-frame-step`
   for sampling. For the rendered video artifact, receipt SHA-256, frame count
   and dimensions are verified. Rendered still sequences verify each image
   against receipt frame indices, byte counts and SHA-256; still images
   extracted from video remain caller-asserted. PQ/HLG signal scopes now use
   their declared transfers and BT.2020 matrix. EXR scope reports expose
   `whiteLuminance` and check it against receipts. Runtime HDR scope evidence
   remains open. PQ scopes now measure decoded peak pixel and per-frame average
   luminance in nits; aggregation is marked incomplete when temporal sampling
   skips frames. Explicit source/output range metadata remain open.
   Receipt-bound video scopes also require the encoded transfer, primaries,
   matrix and limited-range metadata to agree with the declared output. sRGB
   video frames are re-encoded with the sRGB OETF during scope calculation.
4. An agent-callable local ONNX subject-matte provider now emits gated,
   source-bound coverage for non-keyed material. `mask-segment` remains the
   no-model scribble-guided alternative; both feed the same propagation and
   correction-anchor path. Verify segmentation quality on representative
   project footage, including hair, occlusion and multiple salient objects.
   `TemporalMaskPropagator` still performs adjacent-frame CPU propagation, not
   semantic re-detection during occlusion or recovery.
5. Exercise and automate the bounded agent recipe in
   `docs/AGENT_VISUAL_FINISHING_WORKFLOW.md` over existing RenderPlan V3
   layers, masks, mattes, precomps, ordered adjustment effects, tracking masks,
   scopes and export. The recipe now binds scope analysis to the video hash or
   per-image sequence hashes and render receipt, but still requires a separate scopes invocation and has not
   been exercised on project media. The matte propagation command emits a
   RenderPlan update for an encoded matte clip. Lower through RenderPlan; never
   invent a second renderer path.
6. Close software compositing cases for stacked/animated masks and mattes,
   alpha boundaries, precomp nesting and effect ordering. GPU parity stays out
   of scope.

## Acceptance evidence

- Defined frame formats and color transforms round-trip known sRGB, Rec.709,
  BT.2020/PQ, HLG and linear EXR samples within documented tolerances.
- Scope outputs match fixed signal vectors, honor alpha/stride, and report
  clipping/headroom deterministically.
- Subject masks remain temporally stable across motion, occlusion and recovery;
  lost confidence is surfaced instead of silently holding a bad track.
- Agent recipes validate, lower transactionally through RenderPlan, and produce
  inspectable scope and export metadata for the same rendered frames.
- Software compositing acceptance corpus covers combined masks, mattes,
  precomps and effect stacks. No GPU certification is required by this ticket.
