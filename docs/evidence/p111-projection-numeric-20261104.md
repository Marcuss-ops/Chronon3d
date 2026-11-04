# P1-11 camera projection numeric checkpoint — 2026-11-04

Added a consumer-side numeric golden combining a physical 85 mm lens, a
36×24 mm sensor, Fill gate, non-zero lens shift, enabled DoF, and a point at
camera-space depth 1100 on a 1920×1080 viewport. It locks visibility, depth,
vertical focal/depth scale, and the resulting screen coordinate on each axis.

Command:

```sh
cmake --build build/chronon/linux-video-test --target chronon3d_scene_tests -j12
build/chronon/linux-video-test/tests/chronon3d_scene_tests \
  --test-case='combined physical camera projection has a numeric lens-shift golden'
```

Result: 1 test case, 5 assertions passed. This is focused local evidence only.
It does not close P1-11: combined near/far behavior across animated samples,
DoF image behavior, and boundary-case video acceptance remain unverified.
