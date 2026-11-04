# P0-09 Vulkan job lifecycle — 2026-11-04

Focused GPU lifecycle evidence from an NVIDIA RTX A4000. The CLI was built
locally from C3D commit `7972b0c4c` plus no post-commit source changes; its
SHA-256 is `62ab4617f9a6efbeb440182b73716c2afb7a6f68eff312c87c14cc43605a20d1`.
The binary was copied to the remote host; no build ran there. The run used
Vulkan with raw RGBA readback, and enabled both Vulkan validation and sync
validation.

Results:

- 100/100 sequential render jobs completed; each produced a 12,288-byte
  64×48 RGBA frame and a lifecycle receipt.
- Validation scan over all 100 logs: 0 `VUID-` matches and 0 `Validation Error`
  matches.
- Two simultaneous one-frame Vulkan jobs both exited 0 and produced the
  expected raw size.
- SIGINT cancellation during a 150-frame job exited 1 with
  `cancelled=true`; the writer and graph teardown completed, the partial raw
  output was removed, and no render output file remained.
- Earlier focused A4000 lifecycle tests: submission ring 8/8 cases, 3,113
  assertions; upload ring 1/1, 31 assertions; surface materializer 31/31, 471
  assertions; descriptor arena 4/4. Inline-mesh dispatch coverage 2/2 cases,
  108 assertions. See [focused lifecycle log](image-mesh-occlusion-20261103/p009-lifecycle-tests-20261104.log)
  and [100-cycle surface log](image-mesh-occlusion-20261103/p009-100-cycles-20261104.log).

This closes the local GPU job-lifecycle stress gate. It is not a same-SHA CI
result or release certification.
