# Vulkan shadow map acceptance — 2026-11-11

The production Vulkan shadow-map canary passes on the current source revision.
It dispatches the `ShadowCaster` and `ShadowApply` compute kernels and compares
the downloaded GPU result with the CPU PCF reference.

- Source commit: `2303721e2452563afd2af646bda663f5bfde0a17`.
- GPU: NVIDIA GeForce RTX 4060 Ti; driver 610.88.
- Test: `chronon3d_shadow_map_tests.exe --test-case='*Shadow*'`.
- Result: 5 cases, 68 assertions, 0 failures, 0 skipped; includes the CPU
  projection/PCF references and Vulkan GPU canary.
- Test binary SHA-256: `D0C7800C324E741AA3233974E89562F089D9B6B322A1A1E7E55D714A6B9BF35D`.
- Measurements: 32×32 map; shadow edge error 0 px; PCF edge value 0.4;
  PCF softened the edge; adaptive bias removed acne; CPU/GPU max delta 0.
- Machine report: [`report.json`](report.json), status `PASS`.

The test binary was the existing Release artifact from the sibling checkout;
its shadow-map implementation, shaders, test source and contract match this
commit. Same-SHA CI has not been run, so this records local hardware acceptance,
not CI certification.
