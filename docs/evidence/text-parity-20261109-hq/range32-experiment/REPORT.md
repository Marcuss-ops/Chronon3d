# High MTSDF distance-range experiment — 2026-11-09

The `High` profile was changed from 128 em / range 16 / padding 24 to 128 em /
range 32 / padding 48 (`generation_id=3`). The change preserves the Fast
profile's screen-space distance range when em resolution doubles. Two
frame-0 plans were reduced to one frame; their software frame 0 is retained
beside the Vulkan capture. Vulkan used `require_gpu_native` and logged
`route=mtsdf`.

| Case | 128-em/range-16 baseline | Range-32 result | Frozen threshold |
|---|---:|---:|---:|
| `effects` alpha IoU | 0.844884 | 0.904079 | ≥ 0.995 |
| `effects` coverage | 0.844884 | 0.904079 | 0.99–1.01 |
| `multi_shadow` alpha IoU | 0.819951 | 0.931190 | ≥ 0.995 |
| `multi_shadow` coverage | 0.819951 | 0.932887 | 0.99–1.01 |

Both measurements improve materially, but both cases still fail the frozen
gate. The focused runs do not certify the profile; the full corpus rerun at
[`../../text-parity-20261109-range32/REPORT.md`](../../text-parity-20261109-range32/REPORT.md)
is the authority. CLI SHA-256: `86581d06c6635f6caa613bfe0f7d586d55b436287c55d955e59b21f97f2329f9`.

Retained one-frame plan copies, RGBA raws and exact timing sidecars are in this
directory. The one-frame transition-plan probe is intentionally not used: its
duration truncation changes the authored transition semantics.
