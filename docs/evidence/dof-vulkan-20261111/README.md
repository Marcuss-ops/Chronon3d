# Vulkan DoF depth and bokeh canary — 2026-11-11

- Hardware: NVIDIA GeForce RTX 4060 Ti, driver 610.88.
- Test executable: `chronon3d_vulkan_dof_depth_canary_tests.exe` from the
  existing Release build; test source SHA matches source commit
  `2303721e2452563afd2af646bda663f5bfde0a17`.
- Result: 3/3 cases, 47/47 assertions, 0 failures, 0 skips.
- Test binary SHA-256:
  `F1CC29441A9AF8D10D95BEF2C55D7FF1D0663DD0073A0D417F3136B84273FD33`.
- Covered: nearest-depth selection independent of authoring order, a circular
  HDR bokeh footprint, and a sharp foreground bar without far-bokeh bleed.
- Captured single-point response: [`bokeh.png`](bokeh.png); source PGM:
  [`bokeh.pgm`](bokeh.pgm); full output: [`test.log`](test.log).
- PGM SHA-256:
  `D845699C359D2D4BAB8409738D1CAFEBD07C9CDAF2430B0AA789782DD70672B1`.

This is GPU kernel evidence. It does not replace the ticket's integrated
Software↔Vulkan RenderPlan comparison or same-SHA CI gate.
