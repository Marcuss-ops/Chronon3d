# TextOverflow::Clip renderer enforcement — 2026-10-29

## Implementation

The software TextRun renderer now masks the final glyph surface to the authored
layout box in text-local coordinates. Applying the mask before the model
transform keeps the clip aligned under transform scale/rotation. Text shadows
are clipped in the same local box with their displacement accounted for. The
visibility audit bbox is intersected with the box as well. `TextOverflow::Visible`
does not take this path.

The regression renders an oversized word with `Visible` first and proves that
it exceeds the box, then renders it with `Clip` and checks all four pixel bbox
edges. It also checks 3,921 visible pixels, a 59-pixel ink height, and the
materialized layout's unchanged authored font size of 200 px.

## Verification

- Chronon3D HEAD: `014eeb4dfd1703820d49b6382d127ab253bf1449` (dirty tree).
- Build preset: `build/chronon/linux-video-test` (Debug; existing local config).
- Backend: software TextRun with Blend2D; no GPU parity is claimed by this test.
- Builds: `cmake --build build/chronon/linux-video-test --target chronon3d_text_clip_oversized_tests -- -j2` and `cmake --build build/chronon/linux-video-test --target chronon3d_text_layout_advanced_tests chronon3d_text_alignment_isolated_tests -- -j2` — both exit 0.
- CTest: `ctest --test-dir build/chronon/linux-video-test --output-on-failure -R '^chronon3d_text_(clip_oversized|layout_advanced|alignment_isolated)_tests$'` — exit 0, 3/3 suites.
- Direct doctest run with `--success`: 1/1 test, 14/14 assertions passed.
- Peak policy: targeted build with two jobs; no concurrent build or render.

Pixel bbox after `Clip`: `(901,531)–(1020,589)` inside the authored 200×100
box centered at `(960,540)`. `Visible` remains unclipped. Structured sidecar
telemetry is optional per the ticket and was not requested, so it remains
unimplemented.

## Scope

This closes software renderer clipping and its local regression lock. The
ticket does not claim a Vulkan TextRun parity test or clean-SHA CI run; those
are outside this test's evidence.
