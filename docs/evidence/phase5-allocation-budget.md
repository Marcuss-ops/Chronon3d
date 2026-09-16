# Phase 5 — steady-state allocation budget

Status: **HISTORICAL PASS** on the recorded Vulkan baseline; it is not
same-SHA evidence for the current revision.

The benchmark report now carries `steady_state_allocations_per_frame`, fed by
the frame-arena upstream-allocation counter and frame-slot pool hot-path
counter. On `DarkGridBackground`, 4850 measured frames reported:

```text
steady_state_allocations_per_frame=0.0
```

The gate is registered in `tools/gates/manifest.sh` and enforces an explicit
threshold of zero unless overridden by the certification harness.

Recorded baseline: [`steady-state-allocations-certified.txt`](../baselines/steady-state-allocations-certified.txt)
(`source_revision=8bdf0f807`; the current gate reports a revision mismatch).

Gate command/result:

```text
tools/gates/check_steady_state_allocations.sh
GATE_WARN: ... report source_revision=8bdf0f807 != expected <candidate SHA>
GATE_PASS: ... steady_state_allocations_per_frame=0.0 ... revision_state=mismatch
```

Source report: `/tmp/chronon-phase4-gpu-evidence.8dRzRr/single.json`.
