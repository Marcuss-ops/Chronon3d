# Phase 5 — steady-state allocation budget

Status: **PASS** on the recorded Vulkan baseline.

The benchmark report now carries `steady_state_allocations_per_frame`, fed by
the frame-arena upstream-allocation counter and frame-slot pool hot-path
counter. On `DarkGridBackground`, 4850 measured frames reported:

```text
steady_state_allocations_per_frame=0.0
```

The gate is registered in `tools/gates/manifest.sh` and enforces an explicit
threshold of zero unless overridden by the certification harness.

Certified baseline: [`steady-state-allocations-certified.txt`](../baselines/steady-state-allocations-certified.txt)

Gate command/result:

```text
tools/gates/check_steady_state_allocations.sh
GATE_PASS: steady_state_allocations steady_state_allocations_per_frame=0.0 threshold=0
```

Source report: `/tmp/chronon-phase4-gpu-evidence.8dRzRr/single.json`.
