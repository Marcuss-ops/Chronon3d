# Chronon3D v0.1 — canonical release contract

| Field | Canonical value |
|---|---|
| Version | `0.1.0` |
| Tag | `v0.1` |
| Release SHA | `7e86278e5535b799ec5c54960e520ce38c77244a` |
| Release date | 2026-08-28 |
| Certification status | `BLOCKED / NOT CERTIFIED` on the release SHA |
| Historical green baseline | `main@7eb5c2ba`, 11/11 PASS, 2026-07-06 |

## Guaranteed scope

The v0.1 product contract covers RenderPlan, software reference, Vulkan path,
text overlay, watermark/logo, subtitles, video export, NVENC, deterministic
core, and persistent daemon. These capabilities are present in the release
scope, but the release SHA is not promoted to a green certified baseline until
the certification checklist below passes on the same SHA.

## Explicit limitations

Camera advanced, complete zero-copy, multi-GPU, HDR and CopyGop are `PARTIAL`
and are not release guarantees.

## Same-SHA certification checklist

All results must be produced from the exact release SHA above:

1. clean configure and clean build;
2. CTest with no missing required executable;
3. the repository's 11 canonical release gates;
4. determinism corpus;
5. BENCH-1..5 performance baseline;
6. toolchain and host fingerprint;
7. immutable report containing commands, exit codes and artifacts.

A missing toolchain, timeout before executable generation, or stale baseline is
`BLOCKED`, never `PASS`. The tag is immutable; a corrected certification is a
new release/tag, not a rewritten v0.1 tag.

## Authority map

- `RELEASE_V0_1_CONTRACT.md`: this contract and release identity;
- `RELEASE_V0_1.md`: user-facing scope and limitations;
- `RELEASE_GATE.md`: executable acceptance procedure;
- `CURRENT_STATUS.md`: observed current state;
- `ROADMAP.md`: future work only;
- `baselines/main-7eb5c2ba-baseline.md`: historical evidence, not v0.1 SHA evidence.
