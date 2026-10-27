# TICKET-SVG-DOCUMENTARY-VIDEO-PARITY — Documentary SVG graph and video recreation

## Stato

IN PROGRESS — generic SVG import, pixel visibility, path trim sampling, marker
alignment, deterministic software rendering, and SVG text dash output have
focused evidence. The deliverable-specific scene and its reference comparison
remain open. This ticket tracks both the implementation already underway and
the remaining acceptance work; passing generic tests does not certify the
supplied video.

## Obiettivo

Build an editable, reusable `documentary_svg_peak_v1` ChrononTemplate scene
from an SVG asset with stable IDs. Recreate the chart animation and observed
framing/marker motion using the existing RenderPlan V3, Chronon3D SVG import,
and canonical template registries/resolvers/samplers. Use ChrononMotion3D only
for spatial motion that the current native plan cannot express.

## Reference and scene contract

- Reported clip duration: 19.63 s at 30 fps (about 589 frames).
- Timeline from the supplied analysis: vehicle tilt/disappearance at 0–0.5 s;
  thermometer with glowing core at 1–2.5 s; graph dot and line reveal at 3–3.5 s;
  peak line through 6.5 s; presenter at 7–8.5 s; exterior footage and metrics at
  9–12 s; bright point at 12.5–14 s; graph with yellow/red markers and changing
  framing at 15–18.5 s; live footage transition around 19 s.
- Reference samples requested: 3.0/3.5/4.0/4.5/6.0 s and
  15.0/15.5/16.0/17.5/18.5 s. Separate path progression from marker movement
  and overall camera/graphic scale.
- Recreate the opening graphics from authored SVG/C++ assets. The supplied
  video remains reference-only; the presenter segment uses a generated still
  that matches the studio composition but not the filmed person's identity.
  The current graph preview is a graph-animation work sample, not the full clip.
- Scene contract for the full recreation must record assets and sources,
  composition/camera, motion and timing, layer sources, elements to exclude,
  and the representative preview approval criteria before full rendering.

## Existing implementation and evidence

- Chronon3D imports supported SVG paths and styles into existing shape plans;
  the SVG motion showcase exercises an authored SVG through the native renderer.
- `ChrononTemplate/tools/vector_diagram/reconstruct_peak_diagrams.py` creates
  several inline-path diagrams and plans. Its markers are authored separately
  and the peak marker does not follow an SVG ID.
- `ChrononTemplate/tools/vector_diagram/svg_peak_pixel_gate.py` checks imported
  SVG pixel visibility, marker centroid alignment (max error 0.735 px), and
  repeated raw RGBA equality on synthetic geometry. It now also checks the
  composed SVG-plus-follow-path-marker result at f0/15/30/45/60; path coverage
  and marker centroids remain correct with a constant-opacity workaround on
  the SVG layer.
- `scripts/ci/check_svg_trim_sampled_raw_gate.py` checks imported-SVG path
  coverage at frames 0/15/30/45/60: 0/0/67/96/96 pixels; full repeat renders
  match byte-for-byte.
- `scripts/ci/check_svg_text_stroke_raw_gate.py` checks software text stroke
  dash output (solid 2674 pixels, dashed 119) and repeated frame equality.
- The original 19.63-second source is now available at
  `C:\Users\pater\Videos\Screen Recordings\Screen Recording 2026-10-10 204739.mp4`.
  The earlier OneDrive/MP4 search was a search of the requested delivery folder,
  not evidence that the source clip did not exist. Sixteen actual PNG reference
  frames at 0.5-second intervals from 0.0 through 7.5 seconds and a contact
  sheet are extracted under
  `Chronon3d/artifacts/screen_recording_2026-10-10_204739/`.
- A C++ scene authoring entry point writes editable SVG assets and a seven-layer
  RenderPlan for the first eight seconds. The presenter is a Codex-generated
  still at `assets/presenter_generated.png`; the source video is not an output
  layer. The plan validates on the SVG-capable binary. Chronon rendered frames
  through its raw RGBA sink and FFmpeg encoded H.264. The opening, thermometer,
  and graph frames were visually inspected; complete encoded-frame review and
  delivery copy remain pending.
- First-eight-second delivery completed on `main`: Chronon software rendered
  240 raw RGBA frames at 1214×676/30 fps; FFmpeg encoded an 8.000-second H.264
  MP4, verified as 240 frames. A 16-frame contact sheet samples the encoded
  video every 0.5 seconds and was visually reviewed. The MP4 and both source
  and rendered contact sheets are in
  `C:\Users\pater\OneDrive\Documenti\Chronomotion\MP4\`; the 16 extracted
  reference PNGs are in its `Screen Recording 2026-10-10 204739 - frame
  references` subfolder. The source clip is not composited into the MP4.
- A five-second graph-study MP4 now renders at 854×480, 30 fps, 150 frames
  with Chronon3D's software backend and H.264 encoding. Its stable-ID SVG,
  editable RenderPlan, generator, start/middle/hold/end PNGs and contact sheet
  are in `ChrononTemplate/artifacts/documentary_svg_peak_v1/` and
  `ChrononTemplate/tools/vector_diagram/`. The encoded contact sheet was
  visually reviewed; the graph remains in frame and the marker travels along
  it. This is still a graph study, not the supplied documentary clip.
- `ChrononTemplate/catalog/documentary_svg_peak_v1.json` now registers the
  graph-study preset. Its resolver derives follow-path geometry from the
  stable `#peak_line` SVG asset and lowers catalog controls for canvas/timing,
  marker size/color/glow, and framing into RenderPlan V3. Focused catalog tests
  cover determinism, customization, path-derived geometry, and invalid-input
  rejection; the default catalog plan validates and renders through the actual
  software CLI. Glow's generic animated pixel behavior is measured by SVG-04;
  reference-shot appearance remains uncertified.
- Sequential renders of a static SVG under a moving follow-path marker lost
  the path after the marker moved. The compiler classified `Composite` nodes as
  static bake candidates from their declared cache policy, although
  `evaluate_cache` explicitly bypasses inter-frame caching for composites. The
  static-island classifier now excludes composites, and the raw pixel gate
  requires the unguarded SVG-plus-marker render to retain its path at all sample
  frames. This regression fix is awaiting a rebuilt CLI run. The previous
  constant-opacity workaround is removed from the preset. A focused
  moving-marker glow comparison sees 602 changed halo pixels and zero pixels
  at the marker's prior position; reference-scene glow remains open under
  SVG-04.
- Follow-up diagnosis (2026-10-19): the loss is sequence-dependent. Rendering
  frames 49–52 from a cold start preserves all 132 measured line pixels, while
  rendering 0–60 yields 132 at f50 and 6 at f51 onward. In the full sequential
  run, the cached `svg_path_0` framebuffer still has 361 alpha pixels and 188
  bright pixels at f49–f52, so the SVG source cache itself remains intact. The
  `CompositeNode` receives a sparse bottom input (14 alpha pixels) at f50/f51
  and outputs 126/127 alpha pixels; the final graph output changes from 473
  alpha and 208 pixels above the linear 0.25 threshold at f50 to 127 alpha and
  7 such pixels at f51. `finish_frame_encode` and native-output synchronization
  leave those graph-output counts unchanged. The identified compiler/executor
  policy mismatch allowed a composite that bypasses frame caching during
  execution to enter a cross-frame static bake. The compiler fix is under
  raw-pixel verification.
  Temporary environment-gated pixel diagnostics were removed after collecting
  this evidence.
- Additional dirty-rectangle check (2026-10-19): the multi-segment peak SVG
  rendered in two sequential 61-frame runs differs at partial-reveal frames
  when dirty rectangles are enabled, while the same plan is byte-identical
  across runs with dirty rectangles disabled. The existing sampled Trim Paths
  gate uses a simpler shape and does not cover this sequence. A conservative
  path-content-change fallback was added to redraw the full frame only when a
  path-bearing layer changes; its rebuild and raw repeat gate are pending
  because the local vcpkg tree is missing the `boost_spirit` CMake package.

## Acceptance gates

| Gate | Evidence required | Current state |
| --- | --- | --- |
| SVG-01 static pixels | Imported source SVG pixels in Chronon output; inspect representative frame | Generic synthetic gate passes; target frame comparison open |
| SVG-02 Trim Paths | Pixel coverage progression at relevant frames; target timing compared to reference | Generic raw samples pass; target shot timing open |
| SVG-03 marker positioning | Rendered marker centroid on intended path/coordinates while transforms change | Generic marker gate passes (max isolated error 0.735 px); unguarded composed regression gate awaiting rerun; target framing open |
| SVG-04 glow | Core/halo pixel bounds, alpha residue and frame carryover checks | Synthetic combined moving-marker test passes (602 halo pixels, 0 prior-position pixels, deterministic repeat); target visual gate open |
| SVG-05 transforms | CPU importer supports fixed-center rotation and commuting translations; vehicle pivot, scale and graph camera push still require checks against the target rendered frames | Generic implementation PASS; target reference still open |
| SVG-06 temporal stability | Inspect graph/contours across motion; no unintended flicker | Generic repeat determinism exists; target review open |
| SVG-07 composite | Encode SVG and generated presenter image in one timeline and inspect cut/overlay | Rendered; encoded-frame review pending |
| SVG-08 determinism | Repeated raw/video renders match on approved scene plan | Generic 90/360-frame evidence exists; target plan open |
| SVG-09 CPU/GPU parity | Capability-gated comparison on supported Vulkan host | Open |

## Cinematic idle and effect composition follow-up

The supplied motion/effect inventory is a capability starting point, not a
certification that every listed route works together on every layer. Inspection
of current ChrononMotion3D sources found:

- `breathe()` has a four-second sine cycle, but samples only once per quarter
  period (four keyframes per second) and linearly interpolates between samples.
  Continuous smoothness, exact loop joins at 4/8/12 seconds, and per-frame pixel
  continuity still need evidence.
- `floatHover()` currently keys four intervals across its requested duration;
  it is not a continuously evaluated oscillator.
- `floatDrift()` uses fixed 0.03/0.02 scene-unit offsets and a 0.5-degree roll.
  These values are not scale-aware and can be imperceptible in pixel-coordinate
  scenes.
- `breathe()` and entrance presets can both write scale. The layer model has a
  single scale channel, so the interaction must be shown to compose or changed
  to an explicit deterministic composition rule before stacking the presets.
- Glow, Gaussian blur, bloom, vignette, noise, and other effect types are
  present in the Chronon3D render-plan effect decoder/catalog. Availability in
  the decoder alone does not verify animated pixel output, alpha cleanup,
  order-of-operations, software/GPU parity, or successful composition with
  idle motion.
- An earlier noise experiment reported that `animated=true` did not vary the
  decoded pixels. Recheck current behavior before relying on animated film
  grain; use frame-difference evidence with deterministic random access.

Priority certification gates:

| Gate | Required evidence | State |
| --- | --- | --- |
| IDLE-01 continuous breath | Adjacent-frame transform/pixel motion has no reversal kink | Open |
| IDLE-02 loop | Periodic equality or tolerance at 4, 8, and 12 seconds | Open |
| IDLE-03 composition | `scalePop`/entrance plus breathe yields deterministic combined scale | Open |
| IDLE-04 phase | Five layers keep distinct deterministic phases | Open |
| IDLE-05 light pulse | Core/glow pulse changes while preserving alpha and leaving no residue | Open |
| FX-01 blur | Gaussian, directional, and radial blur have clean edges | Open |
| FX-02 grain | Animated seeded noise changes across frames and is random-access deterministic | Open |
| FX-03 stack | Blur + glow + breathing + camera in one plan | Open |
| FX-04 random access | Same selected frame matches sequential rendering | Open |
| FX-05 backend parity | CPU and supported Vulkan output comparison at 1080p | Open |

Apply idle motion only to elements whose reference behavior is idle. Do not
layer breathing onto the van tip, thermometer reveal, graph draw, or presenter
cut in a way that changes their observed timing or geometry. The current
first-eight-second scene does not certify the general idle+FX combination.

## Work plan and completion checklist

- [x] Run the supplied graph analysis against current SVG importer and template
  work; separate demonstrated behavior from historical renderer reports.
- [x] Add synthetic raw-pixel gates for static SVG visibility, sampled Trim
  Paths, marker alignment, and deterministic repeats; update the composition
  gate to require the static SVG path to survive unguarded marker animation.
- [x] Add software text-stroke dash output and a pixel-level dash gate.
- [x] Add nested `<tspan>` inheritable text paint restoration coverage.
- [x] Add a stable-ID SVG graph source asset and editable RenderPlan, plus an
  854×480 five-second software MP4 with visually inspected sample frames.
- [x] Register `documentary_svg_peak_v1` in the ChrononTemplate catalog and
  lower configurable geometry, timing, markers, glow, and framing to RenderPlan
  V3; default plan validates/renders and catalog customization gates pass.
- [ ] Compare the actual rendered chart at the requested reference timestamps;
  correct path, marker motion, and graphic framing independently.
- [x] Add a raw-pixel moving-marker glow gate for halo presence, prior-position
  residue and deterministic repeat; current synthetic result is 602 halo
  pixels and 0 prior-position pixels.
- [ ] Compare glow bounds/residue against the reference scene.
- [ ] Verify vehicle pivot and graph framing transforms against reference
  frames.
- [ ] Review temporal flicker on reference graph contours and markers.
- [x] Encode SVG plus the generated presenter still in one timeline; the source
  clip remains reference-only.
- [ ] Run CPU/GPU parity where Vulkan capability is available and record the
  backend, resolution, frame rate, and duration.
- [x] Produce and inspect an 854×480 graph-study preview and contact sheet;
  verify opening, middle, ending and marker motion start/middle/end.
- [x] Produce and inspect the first-eight-second C++ scene on this `main`
  checkout; render the 240-frame MP4, inspect its 16 half-second samples, and
  copy it into the requested OneDrive folder.
- [ ] Produce and inspect an 854×480 preview of the complete 19.63-second scene.
  For the full clip, sample references at 0.5-second intervals.
- [ ] Compare every encoded first-eight-second frame sample side by side with
  its reference and correct remaining composition differences.
- [ ] Add and execute the IDLE-01–05 and FX-01–05 certification gates above
  before registering a general cinematic-idle preset in ChrononTemplate.
- [ ] Run the focused importer/C ABI tests and same-source checkout CI evidence
  required by the owning SVG/render-plan ticket.
- [x] Verify the static-bake classifier fix with the raw-pixel 0–60-frame
  regression gate. The current software CLI retains all 132 path pixels at
  f0/15/30/45/60, and two 61-frame RGBA renders match exactly.
- [ ] Rerun the preset without the constant-opacity workaround and inspect its
  new encoded preview.
- [ ] Rebuild and verify the animated-path dirty-rectangle fallback against two
  consecutive 61/150-frame raw renders; replace the temporary
  `EffectBoundsUnknown` attribution with a dedicated diagnostic reason if the
  fallback is retained.

## Dependencies and limitations

The source clip is available for analysis only and first-eight-second
references are extracted. Full-clip recreation, side-by-side composition
corrections, reference samples after 8 seconds, and general cinematic-idle
certification remain open. Chronon CLI's in-process video encoder is unavailable
in this build, so Chronon raw RGBA frames are encoded with the installed FFmpeg
CLI. Do not substitute the generic graph-study render or linked illustrative
images as evidence of matching the source. The current graph-study render is
available at
`ChrononTemplate/artifacts/documentary_svg_peak_v1/documentary_svg_peak_v1.mp4`
with its inspected contact sheet alongside it.

Related implementation ticket: [`TICKET-SVG-RENDER-PLAN.md`](TICKET-SVG-RENDER-PLAN.md).
