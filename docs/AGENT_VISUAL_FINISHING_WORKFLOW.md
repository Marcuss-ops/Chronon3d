# Agent Visual Finishing Workflow

This is the bounded CPU workflow currently available for agent-authored
composites, grades, tracked mattes, output and scope evidence. RenderPlan V3 is
the authoring contract; the canonical Chronon renderer and software video
encoder produce the artifact. Vulkan/GPU effect parity is outside this
workflow.

## 1. Author and inspect the plan

Write one RenderPlan V3 JSON containing the composition, asset references,
layer timing and ordering, masks/mattes, precomps, and ordered adjustment
effects. Ordered mask modes include Add, Subtract, Intersect and Difference;
a leading Subtract mask cuts a hole from the full layer coverage. Adjustment
layers can use the software catalog effects, including
Exposure, Levels, Curves, LUT, Temperature/Tint and Lift/Gamma/Gain. Declare the
output color space and the absolute luminance assigned to linear RGB 1.0 in
`output.color_space` and `output.reference_white_nits`. The same declared
reference white scales PQ/HLG encoding and the OpenEXR `whiteLuminance`
attribute. PQ/HLG video may also declare explicit ST 2086 mastering-display
chromaticities/luminance and optional CTA-861.3 MaxCLL and MaxFALL. These are
authored facts; Chronon does not infer them from pixels. Explicit mastering
metadata currently requires native CPU video encoding. The RenderPlan receipt
records the requested values and checks ffprobe's decoded stream side data for
matching mastering/content-light values. Coordinate tolerance is 0.000001 and
luminance tolerance is 0.00001 nit, matching the six-decimal FFmpeg/ffprobe
representation. Missing or mismatched values fail receipt verification and
make the output ineligible for copying.

```json
{
  "output": {
    "color_space": "pq",
    "reference_white_nits": 203,
    "hdr_mastering_metadata": {
      "display_primaries": [[0.708, 0.292], [0.170, 0.797], [0.131, 0.046]],
      "white_point": [0.3127, 0.3290],
      "min_luminance_nits": 0.005,
      "max_luminance_nits": 1000,
      "content_light": {"max_cll_nits": 1000, "max_fall_nits": 400}
    }
  }
}
```

Byte-backed image layers default to sRGB. Set `input_color_space` to
`linearsrgb` for linear-light RGB, `rec709` for Rec.709 encoded RGB or
`displayp3` for sRGB-transfer encoded Display P3/D65 values. P3 inputs are
converted to the renderer's linear BT.709 working primaries; the declaration
participates in the plan fingerprint and image-cache key:

```json
{
  "id": "p3_plate",
  "type": "image",
  "asset": "plates/display_p3_plate.png",
  "input_color_space": "displayp3",
  "size": [1920, 1080]
}
```

This declaration is authoritative for byte-backed PNG/JPEG assets; embedded
ICC profiles are not auto-selected by this path. Tagged EXR assets retain their
separate chromaticity conversion to linear BT.709.

EXR assets remain linear-float and use their tagged chromaticities. Video
assets use stream transfer, matrix, range and primaries metadata with the
decoder's documented BT.709 fallback for unspecified values.

Semantic-script image backgrounds and `image_overlay` events accept the same
`input_color_space` declaration (`srgb`, `linearsrgb`, `rec709`, or `displayp3`); text
events reject it and invalid values fail decoding. For example:

```json
{
  "canvas": {"width": 1920, "height": 1080, "fps": 30,
             "duration_frames": 90},
  "background": {"asset": "plates/linear.png",
                 "input_color_space": "linearsrgb"},
  "events": [{"id": "logo", "kind": "image_overlay",
              "asset": "graphics/logo.png", "input_color_space": "srgb",
              "start_frame": 0, "duration_frames": 90}]
}
```

`chronon3d inspect --plan` reports each image layer's resolved input transfer
in text and JSON, so the agent can verify the canonical plan before rendering.
For every frame in which a target layer is active, its referenced track-matte
source must also be active. A missing or out-of-duration matte now fails the
render with the source layer id and frame; it cannot silently render the target
without its matte.

OCIO scene looks can be baked into 3D LUT assets and placed at a chosen point
in the ordered grading stack. Use the OCIO config's name for Chronon's linear
BT.709 working-space values and a named scene look:

```sh
chronon3d ocio-bake-lut --config show.ocio \
  --working-space "Linear BT.709" --look "warm_key" \
  --size 33 --domain-min 0 --domain-max 16 \
  --output looks/warm_key.cube
```

Reference the generated asset from an adjustment layer:

```json
{
  "id": "scene_look",
  "type": "adjustment",
  "effects": [
    {"type": "lut_3d", "asset": "looks/warm_key.cube", "amount": 1.0}
  ]
}
```

The bake samples the configured OCIO LookTransform over the declared cubic
input domain, returning to the chosen working space before writing `.cube`.
The software grade stack then interpolates that LUT in its authored order.
Raise the domain maximum to cover expected HDR headroom; values outside the
declared domain clamp to the LUT boundary. This exposes creative OCIO looks in
the existing graph; the project can also apply an OCIO display/view transform
at delivery as described below.

The same command can bake an explicit OCIO color-space transform. EXR layer
loading preserves linear float headroom and converts tagged primaries into
linear BT.709 automatically. If an EXR contains ACEScg values but lacks its
chromaticities attribute (so OpenEXR's BT.709 default would be wrong), declare
that source interpretation explicitly and convert the layer before compositing:

```sh
chronon3d ocio-bake-lut --config studio.ocio \
  --input-space ACEScg --output-space "Linear BT.709" \
  --size 33 --domain-min 0 --domain-max 16 \
  --output transforms/acescg_to_linear_bt709.cube
```

Attach it to that source layer's ordered effect list:

```json
{
  "id": "plate",
  "type": "image",
  "asset": "plate.exr",
  "effects": [
    {"type": "lut_3d", "asset": "transforms/acescg_to_linear_bt709.cube"}
  ]
}
```

The input samples at the effect must actually be in the declared OCIO input
space. The conversion is explicit and per-layer; RenderPlan still does not
discover missing source color metadata. Display/view transforms are declared
once on the project color-management object and applied at output.

For byte-backed image inputs, a plan may name the project OCIO config and
scene-linear working space, then name the source color space on each image:

```json
{
  "color_management": {
    "ocio_config": "color/production.ocio",
    "working_space": "Linear BT.709"
  },
  "layers": [
    {"id": "plate", "type": "image", "asset": "media/plate.png",
     "ocio_input_space": "Camera Log"}
  ]
}
```

The config resolves under the mounted project asset root. The declared working
space must be scene-linear and BT.709-compatible with Chronon's renderer. This
route requires an OCIO-enabled build. EXR already decodes to linear BT.709 and
cannot also declare `ocio_input_space`.

For delivery, the same project contract may name an OCIO display and
view. Set `output.color_space` to the transfer expected from that view; the
encoder keeps the declared primaries/matrix/range contract and skips applying a
second transfer curve. For stills and image sequences, PNG output is supported
when `output.color_space` is `srgb`; PNG writes the view's encoded values
directly. EXR rejects display/view transforms and remains scene-linear.

```json
{
  "color_management": {
    "ocio_config": "color/production.ocio",
    "working_space": "Linear BT.709",
    "output_display": "Rec.709",
    "output_view": "ACES 1.0 SDR-video"
  },
  "output": {"path": "out/final.mp4", "color_space": "rec709"}
}
```

`inspect --plan` and the render receipt report the selected display/view.
Output-view rendering uses the software path; an explicit GPU-required render
is rejected. PNG stills/sequences are supported only with `srgb` output and
EXR rejects display/view transforms. Verify the actual view's transfer and
primaries against the declared video output contract; HDR encoder and mux
behavior still require runtime evidence on target media.

For camera/log video, `color_management.video_input_space` supplies a project
default to all video layers. A layer may override it with its own
`ocio_input_space`; `inspect --plan` reports the effective source space:

```json
{
  "color_management": {
    "ocio_config": "color/production.ocio",
    "working_space": "Linear BT.709",
    "video_input_space": "Camera Log"
  },
  "layers": [
    {"id": "wide_gamut_camera", "type": "video", "asset": "media/a.mov"},
    {"id": "other_camera", "type": "video", "asset": "media/b.mov",
     "ocio_input_space": "Camera Log Wide Gamut"}
  ]
}
```

Video layers may also declare `ocio_input_space`. The software decoder applies
the named OCIO transform to transfer-coded RGB before linearization and primary
conversion, then feeds the scene-linear working frame to compositing. The
decoder must be able to read the source YUV matrix/range; unsupported matrix
metadata fails closed. This route disables transformed-frame reuse and is not
eligible for DirectYUV, which does not run the software OCIO input transform.
The same top-level `color_management` object supplies the config and working
space; include that `.ocio` file in the mounted project assets so the render
receipt's asset manifest fingerprints its contents.

Resolve and validate the exact input plan before rendering:

```sh
chronon3d inspect --plan plan.json
chronon3d render --plan plan.json --dry-run
```

Still/sequence writer contracts are checked before rendering: `.png` requires
`output.color_space: "srgb"`, and `.exr` requires `"linearsrgb"`. PNG is
written as sRGB; EXR stores linear BT.709 float pixels. PQ/HLG and Rec.709 video
contracts require an encoded video output.

Reject plan diagnostics and unsupported color contracts before render. The
built-in output contract currently covers sRGB, Rec.709, PQ and HLG with an
explicit reference white, source transfer/primaries metadata, and optional HDR
mastering metadata. OCIO is compiled optionally. RenderPlan declares a
scene-linear BT.709-compatible working space, per-image input spaces and a
project default plus per-layer override for video input spaces. OCIO
display/view output transforms can be declared as a paired `output_display`
and `output_view` and are applied by the software delivery path.

The output contract is derived from `output.color_space` and `output.format`;
the following values are normative, not inferred from the pixels:

| Output | Transfer and primaries | Video matrix/range | Alpha storage |
|---|---|---|---|
| PNG | sRGB transfer, BT.709 primaries | RGB; no YCbCr range | straight alpha; transform runs on visible straight RGB |
| EXR | linear BT.709 scene values | RGB; no YCbCr range | linear premultiplied framebuffer values |
| sRGB video | sRGB transfer, BT.709 primaries | BT.709, limited | opaque; transparent canvas is composited over black |
| Rec.709 video | BT.709 transfer, BT.709 primaries | BT.709, limited | opaque; transparent canvas is composited over black |
| PQ/HLG video | PQ/HLG transfer, BT.2020 primaries | BT.2020 non-constant-luminance, limited, 10-bit 4:2:0 | opaque; transparent canvas is composited over black |

PQ/HLG linear value 1.0 represents `output.reference_white_nits`. HDR mastering
metadata is an authored stream declaration and is checked against the muxed
artifact; it does not alter pixel values. Any color-space/view pair outside
this table must fail plan validation or export instead of inheriting a nearby
format's defaults.

## 2. Propagate an authored matte when needed

For green/blue-screen material, Chronon can generate the seed matte from an
extracted SDR frame using the same CPU chroma-distance key as the software
Keying effect. Key color components are linear BT.709 values; a green screen
uses the defaults below. The command writes an opaque grayscale coverage PNG
and a JSON parameter receipt to stdout:

```sh
chronon3d mask-key frame_0000.png --input-transfer rec709 \
  --key-r 0 --key-g 1 --key-b 0 \
  --threshold 0.20 --softness 0.08 --clip-black 0.02 --clip-white 0.95 \
  --choke 0.01 \
  --garbage-x 0.04 --garbage-y 0.02 \
  --garbage-width 0.92 --garbage-height 0.96 --garbage-feather 0.01 \
  --output seed_matte.png > seed_matte.json
```

Omit all garbage-rectangle options to disable that mask. The rectangle must
remain inside normalized image bounds and specify both width and height.
The JSON receipt includes min/max/mean coverage, soft-edge pixels and the
foreground fraction (coverage greater than 0.5). Set
`--minimum-foreground-fraction` and/or `--maximum-foreground-fraction` to gate
empty or nearly full-frame keys; an out-of-range matte exits with status 2,
reports `output_written: false`, and does not publish a seed PNG.
Use the generated matte as a starting seed, inspect its edge, and add authored
correction mattes where spill, hair, motion blur or occlusion need manual
cleanup. For non-keyed images, agents can provide normalized foreground and
background points to a CPU color-model segmenter:

```sh
chronon3d mask-segment frame_0024.png \
  --input-transfer rec709 \
  --foreground 0.48,0.34 --foreground 0.52,0.44 --foreground 0.55,0.60 \
  --background 0.08,0.10 --background 0.91,0.12 --background 0.12,0.91 \
  --seed-radius 7 --iterations 64 \
  --minimum-foreground-fraction 0.02 \
  --maximum-mean-uncertainty 0.35 \
  --output seed_matte.png
```

The seed points must include visible subject and background pixels.
`mask-segment` fits
regularized full-covariance linear-RGB Gaussian color models, preserving
cross-channel color relationships, diffuses their per-pixel probabilities
with weights that decrease across image edges, and writes a soft matte plus a JSON report.
For RGBA PNG input, source alpha is treated as coverage: fully invisible pixels
do not train the color model, partial coverage weights model samples, and the
final matte preserves source transparency. JPEG input is opaque by definition.
It is a scribble-guided initializer rather than an object-recognition model;
inspect the matte and paint correction anchors for overlapping colors, hair,
motion blur, or occlusion before propagation.
`mask-segment` writes the JSON gate report first and publishes the matte only
when its foreground-fraction and uncertainty gates pass. The report's
`matte_written` field is authoritative; a failed gate leaves it false and
returns exit code 2.

For footage without a useful key color or manually identified scribble points,
an optional local ONNX subject-segmentation provider can generate the first
matte. Install its isolated Python dependency once:

```sh
python3 -m venv .venv-visual-segmentation
. .venv-visual-segmentation/bin/activate
python -m pip install -r tools/requirements-agent-visual-segmentation.txt
export U2NET_HOME="$PWD/.cache/visual-segmentation-models"
```

Generate a soft subject matte and receipt:

```sh
python tools/semantic_subject_mask.py frame_0024.png \
  --model u2netp \
  --minimum-foreground-fraction 0.02 \
  --maximum-foreground-fraction 0.90 \
  --maximum-mean-uncertainty 0.60 \
  --output seed_matte.png
```

The provider runs locally and emits a grayscale coverage PNG plus
`seed_matte.png.json`, bound to the source image SHA-256. The default model is
`u2netp`; choose another locally supported `rembg` model with `--model` for a
different subject domain. Model weights are fetched by the provider into its
model cache on first use, so pin and review the model/license separately for a
production pipeline. A failed foreground/uncertainty gate does not publish a
new matte. Inspect the generated matte and author corrections before using it
as the seed for `mask-propagate`; semantic masks remain model estimates, not
ground truth.

For explicitly tagged SDR/HDR video, `mask-propagate` can decode the clip in
presentation order:

```sh
chronon3d mask-propagate --video-input clip.mp4 \
  --seed-mask seed.png --seed-frame 0 \
  --minimum-mean-confidence 0.15 \
  --minimum-mean-foreground-confidence 0.35 \
  --maximum-foreground-loss 0.10 \
  --output 'matte_####.png'
```

Direct SDR video input requires limited-range BT.709 primaries/matrix and
explicit BT.709 or sRGB transfer. HDR input requires limited-range BT.2020
primaries, BT.2020 non-constant-luminance matrix and explicit PQ or HLG transfer;
unspecified or mismatched metadata fails closed. Decoded frames are temporarily
staged as linear BT.709 RGBA float data, without an 8-bit image round-trip, and
removed when the command exits. PQ photometric confidence uses the decoder's
100-nit units; HLG tracking uses scene-linear values. Preflight enforces the
tracking work budget and an 8 GiB decoded-frame spool limit by default; adjust them with
`--maximum-total-work-units` and `--maximum-video-spool-bytes`. For
caller-selected extraction or Rec.709 transfer interpretation, provide an
ordered PNG/JPEG sequence:

```sh
frames=(frame_*.png) # zero-pad filenames so lexical order matches frame order
chronon3d mask-propagate "${frames[@]}" \
  --seed-mask seed.png --seed-frame 0 \
  --input-transfer rec709 \
  --minimum-mean-confidence 0.15 \
  --minimum-mean-foreground-confidence 0.35 \
  --maximum-foreground-loss 0.10 \
  --correction-mask 48=correction_0048.png \
  --refine-radius 4 --refine-epsilon 0.001 \
  --output 'matte_####.png'
```

`mask-propagate` defaults to sRGB PNG/JPEG frame inputs. Set
`--input-transfer rec709` when the extracted frame code values come from a
Rec.709 video; the command decodes them into its linear BT.709 tracking space.
Direct video mode instead reads its stream transfer metadata. The propagation
report binds each indexed matte to the SHA-256 of the source video, or to the
SHA-256 of its corresponding input image for image sequences. Video reports
also include each source sample's exact integer PTS and rational time base, so
VFR association does not depend on nominal frame rate or rounded seconds. Seed
and correction matte images are coverage data and are not color transformed.
`--refine-radius` enables an optional guided filter on propagated frames. It
uses linear-light frame luminance to align soft boundaries with image edges;
authored seed and correction mattes stay exact. Confidence values and gates
describe tracking before refinement, and the JSON report labels refined frames.
The guided-filter luminance is range-normalized per frame, with epsilon scaled
to preserve its linear-light meaning and avoid overflow on finite HDR values.
Radius 0 disables refinement.

`--minimum-mean-confidence` is an optional frame gate (0 disables it). If any
non-anchor frame falls below the configured threshold, the command writes a
failed confidence gate into its JSON report and does not publish the staged
matte or confidence sequences; `outputs_published` is false. It also skips
matte encoding and plan attachment. Choose the threshold from the clip's
texture and motion; authored seed/correction frames are exempt because their
matte pixels are supplied directly.
`--minimum-mean-foreground-confidence` independently gates the mean tracking
confidence over pixels retained by the propagated matte (alpha greater than
0.01), so a large confident background cannot conceal uncertain subject
coverage. Its default of 0 disables the gate; the report includes its sample
count and measured mean. Authored anchor frames remain exempt.
`--maximum-foreground-loss` separately gates failed tracks among source matte
foreground samples so confident background pixels cannot conceal a lost
subject. Its default of 1 disables this additional gate. The report retains
whole-frame mean confidence and records foreground sample/loss counts.
The preflight report also estimates total tracking work and rejects jobs over
`--maximum-total-work-units` (default 100 billion) before propagation starts;
the hard frame-count ceiling is 10000.

Propagate, encode, and attach the matte in one command (requires a build
with native FFmpeg support):

```sh
chronon3d mask-propagate "${frames[@]}" \
  --seed-mask seed.png --seed-frame 0 \
  --input-transfer rec709 \
  --minimum-mean-confidence 0.15 \
  --minimum-mean-foreground-confidence 0.35 \
  --maximum-foreground-loss 0.10 \
  --correction-mask 48=correction_0048.png \
  --refine-radius 4 --refine-epsilon 0.001 \
  --output 'matte_####.png' \
  --plan-input plan.json --plan-output plan_with_matte.json \
  --target-layer subject --matte-video matte.mp4 --encode-matte-video \
  --matte-layer-id subject_matte --matte-type luma
```

The command writes the mask and confidence PNG sequences, encodes an opaque
Rec.709 grayscale H.264 matte at the plan frame rate using the CPU encoder,
then probes its dimensions/color metadata/frame count and software-decodes every
frame to verify H.264/YUV420P, the exact plan frame rate, dimensions and BT.709
metadata, then compares every coverage pixel with the PNG source (maximum
absolute linear-coverage error 0.02) before adding the matte source and
`track_matte` relation to the new plan. Encoding verification details are
appended to the mask report. The
input sequence must match the target layer's duration; native encoding rejects
looped or time-remapped targets. Builds without native FFmpeg can still use
`--matte-video` with a pre-encoded file. The agent supplies the segmentation
seed and corrections. Current propagation is optical-flow based;
it does not discover subjects or guarantee recovery
through long occlusions. Check confidence and rejection counts, add correction
anchors, then inspect the resulting plan before rendering. Confidence history
is carried between anchors and cannot regain certainty after a failed span;
correction anchors deliberately reset it.

## 3. Render the finished artifact

Render through the canonical software backend. Use an explicit frame rate for
video output and a path whose extension selects the desired container:

```sh
chronon3d render --plan plan_with_matte.json --backend software \
  --output finished.mp4 --fps 24 --report
```

When no matte step is needed, render `plan.json` directly. `--report` enables
the render receipt and execution report. A successful render writes
`finished.mp4.receipt.json`, associating the output artifact with the resolved
plan fingerprint, frame range, dimensions and declared output color contract.

## 4. Inspect scopes bound to that artifact

Analyze the exact video and receipt together:

```sh
chronon3d scopes finished.mp4 \
  --render-receipt finished.mp4.receipt.json \
  --require-render-receipt \
  --max-clipped-fraction 0.001 \
  --max-clipped-chroma-fraction 0.001 \
  --max-non-finite-fraction 0.0 \
  --min-opaque-fraction 0.98 \
  --max-transparent-fraction 0.01 \
  --max-pq-peak-nits 1000 \
  --max-pq-frame-average-nits 400 \
  --output finished.scopes.json
```

For video, scopes verify the artifact hash against the receipt and check frame
count, dimensions and declared transfer/primaries/matrix/range before reporting
waveform and vectorscope measurements. Select luma, RGB parade or alpha
coverage with `--waveform-signal`; alpha mode includes transparent pixels so
agents can inspect matte edges and holes. Each frame report also summarizes
fully transparent, partial and fully opaque pixel counts plus mean alpha, so
an agent can gate a matte/composite without decoding the sparse bins. Keep both JSON outputs with the
delivered video so an agent or reviewer can audit the signal. A receipt binds
the report to the render artifact; it does not prove that a creative grade is
visually correct.
Optional quality gates run per sampled frame and are included under
`quality_gates` in the JSON. Clipping fraction counts valid waveform samples
below or above the requested signal range; ignored transparent/non-finite pixels
are excluded for luma/RGB, and RGB parade counts each channel. A configured
clipping gate fails closed if a frame has no valid waveform samples. Clipped
chroma fraction counts valid vectorscope samples beyond
its configured Cb/Cr range; transparent and non-finite samples are excluded,
and a frame with no valid chroma samples fails this gate. Alpha coverage
fractions count fully opaque or fully transparent pixels. A failed gate still
writes the diagnostic report and exits with code 2. Choose thresholds for the
shot and delivery requirement; they are not universal measures of image quality.
`--max-non-finite-fraction` gates invalid waveform pixels independently from
clipping; set it to `0` to reject any NaN/Inf signal sample.
When any per-frame quality gate is enabled for video, `--video-frame-step`
must be `1` for an overall pass. A larger step still emits sampled scope data,
but the quality gate fails closed and records `source_coverage_complete: false`.
When no thresholds are supplied, the report marks the gate state
`not_requested` and each frame `not_evaluated`; a scope analysis alone is not
reported as a successful quality gate.
PQ luminance gates use decoded pixel measurements and require one video input,
every indexed presentation frame (`--video-frame-step 1`) and a measurement for
each frame. Missing PQ data, multiple inputs or skipped frames fail the gate
closed. The report includes measured maxima, sample count and coverage status.
If the receipt declares HDR mastering metadata, scopes also requires its
ffprobe metadata gate to pass and includes the declared values and gate result
in the scope JSON.
For PQ video, scopes also reports the maximum decoded pixel luminance and
maximum per-frame average luminance in nits. `video_sampling.pq_luminance_measurements`
aggregates those values over sampled frames; when `--video-frame-step` is
greater than one, the result is explicitly marked incomplete for the full
content. These are measurements of the decoded signal, not mastering metadata.

For standalone PNG/JPEG scopes without a render receipt, declare the stored
interpretation when it is not sRGB. `--input-transfer rec709` decodes the
Rec.709 transfer; `displayp3` converts P3/D65 primaries to the scope's BT.709
working space; `linearsrgb` treats the byte values as linear BT.709. Receipt
bound rendered stills use their declared output space automatically. EXR is
already read as linear BT.709 after its chromaticity transform.
For EXR, scopes also reports the standard `whiteLuminance` attribute. With a
RenderPlan receipt, it requires the attribute and verifies it against
`output.reference_white_nits` within `max(0.01 nit, 0.01%)`.

For a rendered still sequence, pass the ordered images and the receipt created
for the output pattern. The receipt records each output frame's index, byte
count and SHA-256; scopes checks those values before analyzing an image:

```sh
chronon3d render --plan plan.json --backend software \
  --output 'frames/frame_####.png' --frames 0-59 --report
frames=(frames/frame_*.png)
chronon3d scopes "${frames[@]}" \
  --render-receipt 'frames/frame_####.png.receipt.json' \
  --require-render-receipt \
  --output sequence.scopes.json
```

The shell glob must preserve the plan's zero-padded frame order. Still images
extracted separately from a video are not listed as render outputs in that
video receipt, so their association remains caller-asserted.

## Current limits

- RenderPlan schedules OCIO source-to-working transforms for byte images and
  video. The project working space must be scene-linear and BT.709-compatible.
  Paired OCIO display/view output transforms are applied by CPU video and PNG
  output; EXR rejects them. Arbitrary project working primaries are not
  supported.
- RenderPlan output color-space declarations are checked against the supported
  set (`srgb`, `rec709`, `linearsrgb`, `pq`, `hlg`), and reference white must be
  finite and in `(0, 10000]` nits. This validates the declared output contract.
- Native CPU PQ/HLG output has a declared transfer and 10-bit path, but a
  complete reference-signal runtime evidence chain is still open. Optional
  mastering-display and content-light metadata can be authored in RenderPlan
  and is attached by the native CPU encoder; the receipt checks that ffprobe
  reports those values. A real muxed HDR output and reference-signal render
  have not been exercised. GPU HDR output and HDR matte propagation are not
  part of this recipe.
- EXR layer loading, scopes and image export preserve linear float headroom.
  The loader reads tagged primaries, adapts the source white to D65 with
  Bradford when needed, and converts to linear BT.709; untagged files use the
  OpenEXR BT.709 default. Invalid or singular EXR chromaticities fail closed.
  EXR output tags BT.709/D65 and writes standard `whiteLuminance` from
  `output.reference_white_nits`, defining the nits scale for linear RGB 1.0.
  `scopes` reports that EXR attribute and checks it against the render receipt.
  Video mastering-display metadata can be declared; confirming its preservation
  in the encoded bitstream remains open.
- Mask propagation expects an authored or model-generated seed, ordered decoded
  frames and any needed correction masks. Optional guided-filter edge
  refinement is available; every model matte still needs visual review.
- Scope receipts bind the encoded video. Scope artifacts do not yet become a
  first-class RenderPlan output or an atomic render-and-scope job product.
- Software compositing still needs a dedicated acceptance corpus for combined
  animated masks, matte alpha boundaries, nested precomps and effect ordering.
