# Documentary Snapshot connected recipe review — 2026-10-21

All eleven recipes were lowered by the connected `DocumentarySnapshotPack`
canary and rendered through the release Chronon3D CLI on the software backend.
Frames 0, 53, and 120 were retained as raw RGBA plus verbatim PNG projections.
All 33 renders returned success; the PNG contact sheet was visually reviewed.

Recipes: `pullback-reveal`, `push-through-snapshot`, `whip-to-photo`,
`focus-drop`, `reveal-90`, `corner-turn`, `foreground-photo-pass`,
`photo-stack`, `filmstrip-handoff`, `split-depth`, and `archive-crane-reveal`.

Review confirms the expected intermediate title/photo, portal, focus, turn, and
multi-photo states, and the final photo-card compositions at frame 120. The
pullback-reveal title extends beyond the viewport during its mid-transition,
then returns to the fitted final composition. This review closes the sampled
recipe visual gate only; it does not prove every intermediate frame or
temporal-shutter Vulkan behavior.

`review-contact.webp` contains the 33 reviewed PNGs. Each recipe directory
contains its lowered RenderPlan, three raw frames, three projected PNGs, and
timing sidecars; the adjacent `.log` files capture each invocation. The
connected `DocumentarySnapshotPack` canary in ChrononTemplate produced the
plans; invoke that producer from its owning checkout to regenerate them.

### Vulkan follow-up

The original 90°/8-sample plans were also rendered on Vulkan at frame 53 for
all eleven recipes and visually compared against the software samples.
`focus-drop` and `split-depth` output exactly zero RGBA; `reveal-90` shows a
blank card in place of its photo. Additional Vulkan transition probes cover
frames 45, 46, 54, 90, and 120 with a diagnostic 1°/2-sample shutter. These
results do not close strict-native Vulkan or full-shutter temporal gates. See
[`gpu-fullshutter-review/README.md`](gpu-fullshutter-review/README.md).
