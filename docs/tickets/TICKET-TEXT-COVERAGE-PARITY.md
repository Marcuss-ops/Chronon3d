# TICKET-TEXT-COVERAGE-PARITY

Goal: one text-coverage contract, shared by every backend, correct for any stroke
width and any scale. Fixes the shipped defect where the phrase outline renders
~1px on one side of the glyph with a hard, unantialiased fill.

Status: RESOLVED (2026-09-25)
Owner: Chronon Core
Resolution:
- Phase 1 (PR 1.1, PR 1.2, PR 1.3): Resolved lane observability added across Daemon startup, CLI banner (independent of .quiet), RenderReceipt / job output (`render_lane`, `lane_reason`), and typed `LaneDecision` / `LaneReason` structured fallback logging.
- Phase 2: Fixed CUDA NV12 MTSDF coverage and stroke formula in `cuda_nv12_kernels.hpp` to evaluate field distance and stroke in destination pixels matching Vulkan's `text_tile_raster.comp`.
- Phase 4: Parity contract verified via `cuda and vulkan mtsdf coverage agree for stroke widths` in `tests/media/nv12/nv12_equivalence_test.cpp` for stroke widths {0.0, 1.5, 3.5, 8.0} and scales {0.25, 0.31, 1.0} with tolerance <= 1/255.
- Profiling & Repeatability: RenderCounters extended with `scene_eval_wall_ms_total`, `compositor_wall_ms_total`, `encode_wall_ms_total`, `gop_byte_identical_hits`, `gop_total_encoded`; `tools/audit_corpus_repeatability.sh` added.
Found while diagnosing: live overlay phrase frames (job_1790350674620243798_0596a497)

---

## 0. The measured defect (do not trust the numbers below blindly — see Phase 0)

Frame `isabelle-phrase.png`, 1920x1080, text band y=509..580. A single scanline
through the `A` stem, row 540:

```
x      277   278   279 ... 288   289   290   291   292
luma   226   228   248 ... 254   251     1   233   230
                     ^^^^^^^^^^               ^^^
                     fill, 9px wide         1px of near-black
```

Authored stroke is 3.5px (`official_presets_text.go:44`, `#111827`).

Three observations that cannot all be true at once:

1. The outline is **1px**, not 3.5px.
2. It is present **only on the right** of the fill, never on the left. A
   distance-field outline is symmetric by construction, so a one-sided
   1px result is *not* explainable by the stroke formula alone.
3. The fill edge is a hard step (226 → 254 in one pixel), i.e. no coverage ramp.

### The discrepancy that Phase 0 must resolve

`cuda_nv12_kernels.hpp:449` predicts a symmetric band of `stroke_width`
(= 3.5px) outward from the silhouette, plus 0.5px of outward bias:

```c
stroke_alpha = clamp(((a - 0.5f) * range + stroke_width) * source_scale + 0.5f, 0, 1)
```

With `range = 16` and `source_scale ≈ 1` that is a 3.5px ring. The frame
shows 1px on one side. **Therefore at least one of these is false:**

- the live job did not render through the DirectYUV/CUDA lane (it may have
  fallen back to the Vulkan or software path, where the formula differs);
- `source_scale` is materially smaller than 1 (the layer rect is not the
  atlas display size);
- the alpha lane is not the signed distance the comment claims it is;
- the 1px dark band is the drop shadow (`params.style.shadow`, offset
  `[0,4]`, blur `12`, opacity `0.72`), not the stroke.

Phase 0 settles this before any code changes. **Do not start at Phase 2.**

---

## Phase 0 — Prove which lane rendered the frame (blocking)

There is no log line that answers this today: the daemon
(`apps/chronon3d_daemon/main.cpp:89-93`) prints the *requested* backend
preference and never the resolved lane, and it has no reference to
`direct_yuv` at all. The CLI does log the successful selection
(`pipe_export_pipeline_setup.cpp:258`, `[direct-yuv] selected for video
source '{}'`), but the live worker goes through the daemon.

Three artifacts, cheapest first:

**0a. Search the live logs.**
```bash
journalctl -u renderinggen-queue.service --since '30 min ago' --no-pager -o cat \
  | rg 'direct-yuv|selected|backend'
```
If `[direct-yuv] selected` is absent, the lane is Vulkan or software and the
CUDA kernel is the wrong file to patch.

**0b. Dump the atlas for the phrase layer.** Add a temporary, gated debug
dump behind an env var (`CHRONON3D_DUMP_MTSDF=1`) in
`text_texture_cache.cpp:186-193`, writing the packed atlas to a `.bin` plus
its `distance_range_px`, `stroke_width`, `display_width/source_width`. Then
decode the alpha lane in Python and measure, on a real glyph, the distance in
texels from the 0.5 crossing to the point where it saturates. That number
tells you whether the alpha lane is a true signed distance with
`range = 16`, or something else.

**0c. Compute `source_scale` at the call site.** In
`direct_yuv_program.cpp:256-260` log `rasterized->width`, `display_width`, and
the resolved `dst_x0/dst_x1`. `source_scale` is
`0.5 * (|dst_x1-dst_x0|/source_width + |dst_y1-dst_y0|/source_height)`
(`cuda_nv12_kernels.hpp:431`). If it is ≈ 1, hypothesis 2 is dead.

Record the outcome in this ticket before proceeding. Whichever hypothesis
survives determines whether Phase 2 is the correct fix at all.

---

## Phase 1 — Make the lane observable (prerequisite for measuring everything after)

Three gaps from the audit. Together under 30 lines. This phase is not
optional: without it Phase 4 has no pass/fail signal, and the next person
repeats Phase 0 from scratch.

**1a. Daemon startup: resolved lane, not requested preference.**

`apps/chronon3d_daemon/main.cpp`, after `RenderEngine initialised.`
(line 110). The existing line 93 logs `args.backend`, which is what the
operator asked for. Add the resolved capability set:

```cpp
spdlog::info("  vulkan={} cuda={} software={}",
             engine->has_vulkan(), engine->has_cuda(), true);
```

**1b. Structural lane decision, so a deliberate fallback is not logged as a
failure.** Today every early return in `DirectYuvProgram::prepare` is
indistinguishable, and the caller logs all of them at `error`
(`pipe_export_pipeline_setup.cpp:253`). A composition with a 2.5D layer
falls back to the FullGraph RGBA path (`direct_yuv_program.cpp:105-120`) —
that is a *decision*, not a fault, and it produces a visually different
render that nobody can currently discover. Split the string out-param into

```cpp
enum class LaneDecision { DirectYuvSelected, FallbackUnsupported, Failed };
struct LaneOutcome { LaneDecision decision; std::string code; std::string detail; };
```

with codes like `unsupported_2_5d_layer`, `unsupported_mask`,
`error_scene_eval`, `error_asset_missing`. Log `FallbackUnsupported` at
`info` with the code, `Failed` at `error`.

**1c. Lane in the job result.** The JSON we inspected
(`/tmp/job_*.full.json`) carries `overlay_plan`, `overlay_render`,
`artifacts`, `timing`, `stage_progress` — and nothing identifying which
backend produced the pixels. Add a sibling of `timing`:

```json
"render_backend": {
  "lane": "direct_yuv_cuda",
  "decided_at": "dispatcher",
  "code": "ok",
  "fallbacks_declined": []
}
```

The timing sidecar and `RenderCounters` already exist
(`TICKET-ZERO-COPY-ZWAVES.md:321-329`); this is one field, not new
infrastructure.

**1d. Startup lines must survive `quiet`.** `render_job_setup.cpp:82-83`
sets `.quiet` for anything below `debug`, and `set_log_level` exists only in
the CLI (`register_render_commands.cpp:202-207`). A one-line-per-job
operational banner (lane, encoder, `WxH@fps`) belongs in a channel that
`quiet` does not suppress.

---

## Phase 2 — Fix the coverage formula (only if Phase 0 confirms CUDA)

`src/media/video/compositor/cuda/cuda_nv12_kernels.hpp:441-455`.

Three defects in six lines:

```c
// (i)   fill has no coverage ramp at all
const float fill_alpha = signed_distance > 0.5f ? 1.0f : 0.0f;
// (ii)  outline expands outward by the FULL width, never inward
// (iii) stroke_width, authored in final-frame pixels, is scaled by
//       source_scale, an atlas->destination factor
const float stroke_alpha = fminf(fmaxf(
    ((a_channel - 0.5f) * range + layer.stroke_width) *
        source_scale + 0.5f, 0.0f), 1.0f);
```

The Vulkan backend already implements the intended semantics
(`src/backends/vulkan/shaders/text_tile_raster.comp:406-409`) and explains them
in a comment: *"`stroke_width` is the authored OUTLINE width, therefore the
signed field is expanded by its radius, not by the full diameter."* Make
DirectYUV match:

```c
// One signed-distance ramp in destination pixels, shared by fill and
// outline. `stroke_width` is authored in destination pixels, so it is NOT
// scaled by source_scale: the field is converted, the width is not.
const float field_px = (a_channel - 0.5f) * range * source_scale;
const float fill_alpha =
    fminf(fmaxf(field_px + 0.5f, 0.0f), 1.0f);
const float stroke_alpha = fminf(fmaxf(
    field_px + 0.5f * layer.stroke_width + 0.5f, 0.0f), 1.0f);
```

This fixes all three: the fill regains antialiasing, the outline becomes
centred at `0.5 * stroke_width` per side, and the width is honoured at the
authored size. The `+ 0.5f` places the 0.5 coverage crossing exactly on the
silhouette, so fill edge and outline inner edge coincide instead of being
half a pixel apart.

**Constraint to check first:** the outline can only be expressed inside the
prepared field. `0.5 * stroke_width` must satisfy

```
0.5 * stroke_width + 0.5  <=  range * source_scale     (field range)
0.5 * stroke_width + 0.5  <=  padding_px              (atlas padding)
```

With the canonical profile (`mtsdf_profile.hpp:12`: `range 16`,
`padding 24`) this holds for `stroke_width <= 27`. The public contract
allows up to 64 (`contracts/overlay-plan.v1.schema.json:111`), so at the top
of the range the outline is clipped. Phase 3 removes that ceiling; Phase 2
should additionally *fail loudly* rather than silently clip, which is Phase 3's
`throw`.

---

## Phase 3 — Per-run profile (the architectural fix; Phase 2 is the tactical one)

`include/chronon3d/text/mtsdf_profile.hpp` declares the three constants
immutable, and the comment is emphatic about it. That immutability is the
root cause: a run whose stroke does not fit the field has no way to ask for
more room, so the only lever left is the authored stroke width — which is
what the previous fix attempts were turning.

The generator is already parameterised
(`src/backends/text/mtsdf_generator.cpp`, `generate_mtsdf_glyph(guard,
glyph_id, em_px, distance_range, padding)`). Only the *caller* is pinned to
the constant. Derive instead of declaring:

```cpp
// The field must express the authored outline plus one pixel of coverage
// ramp on each side, or the outline is silently clipped at the padding.
[[nodiscard]] MtsdfProfile profile_for_run(float stroke_width) noexcept {
    MtsdfProfile p{};                        // em_px 64, generation_id 1
    const float needed = 0.5f * std::abs(stroke_width) + 1.0f;
    p.distance_range_px = quantize_up(std::max(needed, 4.0f));
    p.padding_px      = quantize_up(needed + 2.0f);
    return p;
}
```

Quantise to a small ladder (4, 8, 16, 24, 32) rather than arbitrary values,
so that the number of distinct profiles — and therefore the number of
distinct glyph-cache entries — stays small.

**Call sites, all four** (they must change together or the backends drift
again, which is how this bug was born):

| file | line | role |
|---|---|---|
| `src/media/video/direct_yuv/text_texture_cache.cpp` | 92 | `constexpr auto profile = kCanonicalMtsdfProfile;` |
| `src/render_graph/nodes/text_run/gpu_text_run_mtsdf.cpp` | 62-68 | Vulkan atlas profile + `kProfileId` |
| `src/render_graph/executor/node_runner_fused_batch.cpp` | 120-123 | fused batch distance range |
| `include/chronon3d/media/video/cuda_layer_resource.hpp` | 36-37 | default on the resource |

**Cache identity — the part that is easy to get wrong.**
`PreparationCache::GlyphKey` (`mtsdf_preparation.cpp:52-58`) is
`{font_digest, glyph_id, profile}` with a defaulted `operator==`, so all
four profile fields already participate in *equality*. But `GlyphKeyHash`
(lines 60-70) mixes only `key.profile.em_px` — range and padding are absent.
That is not a correctness bug today (a collision merely misses), and it
stops being harmless the moment the profile becomes dynamic: every distinct
range lands in one bucket. Mix all four fields.

Separately, `GpuGlyphKey::make` carries `generation_profile` as a single
`uint16` (`gpu_glyph_atlas.hpp:79-88`), and `gpu_text_run_mtsdf.cpp:136`
passes `kProfileId` from the constant. With a dynamic profile this id must
encode the quantised range, or the Vulkan atlas will serve glyphs generated
for a different field to a run that asked for another. That is a **silent
cross-contamination** and is the single riskiest line in this phase.

---

## Phase 4 — Tests

`tests/media/nv12/nv12_equivalence_test.cpp` already replicates the kernel
maths (it uses `resource.distance_range_px` at line 539), so extend it rather
than starting a new suite.

**4a. Parity across backends — the test whose absence caused this bug.**
Feed one synthetic distance field and one `stroke_width` through the CUDA
formula and the Vulkan formula. Assert the two produce the same silhouette
within one texel of tolerance. Today they differ by a factor of two plus a
`source_scale` factor; that assertion fails immediately and stays failing
until nobody can reintroduce the divergence.

**4b. Coverage ramp.** A synthetic field, sampled across a straight edge.
Assert fill alpha is monotonic and that the 0.5 crossing lands on the edge.
Rejects a future `> 0.5f ? 1.0f : 0.0f`.

**4c. Outline symmetry.** For a uniform slab field, the outline band measured
outward equals the band measured inward, and equals
`0.5 * stroke_width` in destination pixels at `source_scale == 1`.

**4d. Profile bounds.** `profile_for_run` must satisfy the Phase 2
inequalities for every quantised rung, for `stroke_width` at both contract
limits (0 and 64).

**4e. Cache identity.** Two `GlyphKey`s differing only in
`distance_range_px` must not compare equal, and must hash differently once
`GlyphKeyHash` is fixed.

**4f. Golden frames.** A small fixed composition, one phrase, reference PNGs
at two `source_scale` values. Catches everything the arithmetic tests miss.

---

## Phase 5 — Undo the tuning that was compensating for the bug

Two changes were made to mask the real defect and must be revisited once the
formula is correct:

- `renderinggen/internal/overlay/official_presets_text.go:44` — stroke was
  raised 3.5 → 6 "to survive delivery", then lowered back to 3.5 with a
  comment calling the thick edge "a sticker". 3.5 was the design intent;
  keep it, now that it is actually honoured.
- `refactored/config/channel_profiles.yaml:92` — `phrase_apple_clean_04_blur_
  tracking_drift` was removed from the crime rotation because it "lost the
  stroke". Re-add it. If blurring the text during the entrance really does
  fade the outline, that is a property of the motion and the preset, not a
  reason to shrink the rotation.

---

## Sequencing

Phase 0 → 1a/1d → 2 → 4a/4b/4c → 3 → 4d/4e → 4f → 5.

Phase 2 before Phase 3 is deliberate: it is a six-line change that fixes the
shipped visual defect, and it is independently verifiable. Phase 3 removes
the architectural ceiling but touches four backends and two cache keys, and
should not be attempted while a known-wrong formula is still in the tree.

## What this deliberately does not do

- Does not touch the compositing order (painter's algorithm) or introduce
  temporal execution. Those are the larger performance work; mixing them into
  a correctness fix would make the fix unreviewable.
- Does not change the software/Blend2D path. It already strokes centring via
  `ctx.setStrokeWidth` (`raster.cpp:280`); it needs the parity test, not a
  formula change.
- Does not attempt the fill's binary coverage in the software path, which
  goes through Blend2D and antialiases natively.
