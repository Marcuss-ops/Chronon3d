# Performance audit — October 2026

Status: ACTIVE. Scope: local Chronon3d worktree; preserve all pre-existing changes.

## Goal

Remove confirmed resource/measurement defects and bounded hot-path overhead,
without weakening residency, aliasing, pixel correctness, strict GPU policy or
memory admission. No percentage speedup is accepted without before/after runs.
No push/deployment is part of this work.

## Acceptance checklist

- [x] Record pre-existing diff and build configuration; Release Vulkan/CUDA/native
      FFmpeg and ccache are available. Build cap is 20 jobs; monitor RAM.
- [x] Preserve fractional cache/dirty timings before integer-ms conversion;
      regression tests cover concurrent samples, reset and copy.
- [x] Close external memory/semaphore FDs on every failed export/import;
      retain transferred descriptors and destroy partially imported CUDA objects.
- [x] Pipeline cache validates bounded length/vendor/device/UUID, includes driver
      and embedded shader fingerprint in filename, tolerates storage failures,
      and publishes complete files atomically under concurrent writers.
- [ ] Reduce binding scratch allocations without changing carrier lifetime,
      collisions, placement or output; record representative before/after data.
- [ ] Evaluate dirty coalescing/effect scratch and adopt only validated changes.
- [ ] Measure available native CLI lifecycle/handoff and verify output frames and
      zero-copy counters; do not remove fences or admission guards by inference.
- [ ] Rebuild final touched targets and run relevant C++/architecture/tool gates.
- [ ] Record final results and exact remaining blockers.

## Evidence location

Ignored local logs/artifacts: `.tmp/performance-audit/`. Original tracked diff
snapshot: `/tmp/chronon3d-audit-preexisting-20261004.diff`; original untracked
path census is saved alongside it. These are not release artifacts.

## Verified checkpoint

- Focused existing C++ test sources: 12/12 cases, 66/66 assertions PASS;
  repeated under ASan/UBSan with leak detection: PASS.
- Architecture gate: 206/206 PASS on the checked worktree (rerun final).
- Common performance gate, steady-state allocation gate, shader-layout contract,
  GPU corpus manifest, admission source contracts and memory-gate self-test PASS.
- Pre-optimization native CLI reference: grid and `black_glow_inter`, 30 frames
  each, MP4 + sidecar PASS; grid zero-copy: 19/19 PASS. These runs overlapped a
  CPU build, so they are smoke/lifecycle evidence, not a speedup reference.
- Full suite build exposed FFmpeg profile naming incompatibility in existing
  packet-assembler tests; replaced `AV_PROFILE_H264_HIGH` with the identical
  `FF_PROFILE_H264_HIGH` already used by production, preserving assertions.
- Existing export-fence wait counters were never populated. Their production
  wait sites are now instrumented; no fence or synchronization policy removed.

## Scope boundaries

Broad external B00–B11/text content-pack certification, remote runner dispatch,
and daemon production-worker measurements are separate acceptance gates. Their
absence cannot be represented as PASS. Persistent-job resources, fence removal,
sparse native video and job concurrency changes require measured correctness
and memory budgets, not merely successful compilation.
