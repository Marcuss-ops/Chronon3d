# Static GEL reference match — 2026-11-06

The provided Drive item `1T4d9pV5pEmwpiG-x63f7hKaiH2gmDZ5j` is a 1094×143
PNG still, not a video clip. The authenticated download is
`drive-reference-full.png`; its clean background plate is
`docs/evidence/gel-text-stress-20261105/gel-reference-plate-20261106.png`.

The initial Georgia Bold pass was proportionally serifed and did not match the
reference's typewriter-like monospaced slab face. A calibrated follow-up uses
Liberation Mono Bold, font size 62, scale `[1.16, 1.03]`, and the measured word
centers. The font binary is preserved beside this evidence as
`LiberationMono-Bold.ttf`; its upstream package identifies SIL Open Font
License 1.1. The copyright/license notice is preserved as
`Liberation-font-copyright.txt`. The renderer's ignored test-font asset bundle
was left unchanged; plans reference this evidence-local font explicitly.

Files:

- Target: `drive-reference-full.png`
- Render: `reference-word-spacing.png`
- Exact RenderPlan: `reference-word-spacing.plan.json`
- Raw Vulkan frame: `reference-word-spacing.rgba`
- Font used: `LiberationMono-Bold.ttf`

SHA-256:

- Target: `179aea0bdc26e23cae844e2381232d836f53e261a5eb429060d84e148393a959`
- Font: `29534b0aad22fa386843365da6f04102df8d6a076e06a2a5ce270f83dd1bf351`
- Render PNG: `40eb8f7c32b28d797e88b366b9882186039e2b8af16bc6d1a9709b9e8272623b`
- Render raw RGBA: `f44a883881f9f0f46ca9a4c8d018fb779e552cd4c930b2bbb0d6452bacc1f3b7`

Visual gate: **partial PASS** for the font character, word spacing, plate
placement, gel surface, and glow. The font now follows the reference more
closely; body density and highlight color/shape still differ. No exact pixel
match is claimed. The provided still cannot validate an animated footage
composite; the separate 2.5D/DoF probe and its limitations are in
`DOF-EXPERIMENT.md`.
