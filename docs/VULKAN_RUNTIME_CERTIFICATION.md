# Vulkan runtime certification

## Contract

The runtime certification covers the existing Vulkan lifetime tests and does not
introduce a new gate:

- `VK_LAYER_KHRONOS_validation`
- synchronization validation via `CHRONON3D_VULKAN_SYNC_VALIDATION=1`
- lifetime-disjoint physical resource aliasing
- deferred surface release and frame-transient retirement
- submission-ring fence reuse
- final destruction after GPU work completion
- repeated/soak execution with zero validation errors, use-after-free, double
  destruction, or leaks

The canonical focused tests are:

```text
chronon3d_vulkan_debug_context_tests
chronon3d_vulkan_descriptor_arena_tests
chronon3d_vulkan_submission_ring_tests
```

Runtime invocation must use:

```bash
CHRONON3D_VULKAN_VALIDATION=1 \
CHRONON3D_VULKAN_SYNC_VALIDATION=1 \
CHRONON3D_VULKAN_VALIDATION_FAIL_ON_ERROR=1 \
ctest --test-dir <build> -R 'chronon3d_vulkan_(debug_context|descriptor_arena|submission_ring)_tests' --output-on-failure
```

## Host evidence — 2026-08-28

The host exposes:

- `VK_LAYER_KHRONOS_validation` 1.3.204
- NVIDIA RTX A4000, driver 595.84
- Vulkan instance 1.3.204 / device API 1.4.329

The focused build was started from the existing `linux-fast-dev` build tree,
but did not complete within the 120-second execution window. CMake regenerated
the tree and Ninja reported its known `1.13.0` premature-EOF warning while
building 224 targets. No test executable was produced and no runtime validation
result was collected.

## Verdict

```text
PARTIAL / BLOCKED
```

Layer availability and test coverage are present, but aliasing, deferred
release, retirement, destruction, validation, and soak behavior remain
uncertified until a working build host completes the focused build and runs the
suite with the environment above. No PASS is claimed from static inspection.
