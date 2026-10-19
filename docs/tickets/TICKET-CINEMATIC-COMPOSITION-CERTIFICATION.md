# Cinematic composition certification — T16–T18

## Observed result (2026-10-19)

The RenderPlan → Chronon3d path now produces an actual three-layer T16 still
at 1920×1080: procedural night gradient, glowing title, and alpha cutout in
front of the title. The render was made by the video-enabled Chronon3d CLI on
the software backend, then extracted from its H.264 frame. Artifact and pixel
gate: [`output/T16_Hero_Title/README.md`](../../output/T16_Hero_Title/README.md).

The render exposed and fixed a correctness defect in early-exit analysis:
`ImageShape.opacity` was treated as proof that all decoded pixels are opaque.
Transparent full-canvas images could therefore retire the title/background
subtree below them. `SourceNode::can_seed_full_frame` now returns false until
the asset resolver can prove decoded alpha coverage. This is conservative and
may forgo an optimization for opaque full-frame images.

## Certification status

| Gate | Status | Evidence / remaining work |
|---|---|---|
| G01 Camera transport parity | PASS (focused) | Existing Motion → RenderPlan → decoder parity tests pass locally; same-SHA certification remains separate. |
| G02 Camera orientation parity | PASS (focused) | Camera orientation/projection focused tests pass locally. |
| G03 Rack-focus optical quality | PARTIAL | T10 measured ROI sharpness and rack focus, but quality/integration gate remains PARTIAL. |
| G04 Temporal camera determinism | PASS (focused) | T10 direct frame 77 equals sequential frame 77 byte-for-byte on the same backend. |
| G05 Mask temporal stability | PARTIAL | Matte and alpha edge behavior exist; representative video review and temporal edge gate remain. |
| G06 3D occlusion + alpha | PARTIAL | T16 still proves title behind a transparent subject cutout; moving depth/alpha composition remains untested. |
| G07 Dynamic light/shadow | PARTIAL | Existing light/shadow routes and tests exist; animated scene integration remains. |
| G08 Motion blur + DoF | PARTIAL | Individual paths exist; combined moving-object/camera certification remains. |
| G09 GLB material fidelity | PARTIAL | Import/render paths exist; trusted golden comparisons remain. |
| G10 RenderPlan produces pixels | PASS (T16 still) / PARTIAL (end-to-end) | 1920×1080 T16 PNG has checked background/title/foreground samples; 150-frame T16 and T17/T18 compositions remain. |
| G11 Strict GPU path | PARTIAL | T10 NVIDIA/EGL/NVENC lane passed; end-to-end T16 strict GPU and copy/fallback accounting remain. |
| G12 Resource/performance | PARTIAL | T10 timing and peak RSS recorded; VRAM and representative T16–T18 metrics remain. |
| T16 Cinematic Hero Title | PARTIAL | One real Chronon-rendered 1920×1080 frame; 150-frame push-in, approved matte, edge and temporal gates remain. |
| T17 Floating Product Orbit + Rack Focus | PARTIAL | T10 rack-focus probe exists; four-product composition and presentation-quality ROI certification remain. |
| T18 Documentary Crowd + Particle Reveal | NOT RUN | Integrated crowd, animated matte, depth-sorted particles and full clip gate remain. |

## Next acceptance work

1. Add a regression test that keeps lower layers alive for a transparent
   full-canvas source and preserves the opaque-image optimization only when
   decoded alpha coverage is known.
2. Promote T16 from a still to the full 150-frame 1920×1080/30 fps plan and
   compare random-access frames with sequential output; review matte edges
   against approved reference material.
3. Complete T17 ROI sharpness/occlusion gates and T18 temporal matte/particle
   gates using the existing certification registry and threepp path.
4. Record same-SHA GPU/software timing, memory, fallback and copy metrics for
   those end-to-end cases before marking them certified.
