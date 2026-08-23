# TICKET-TOOLS-ANALYZE-FRAMES-ORPHAN-V1 — Remove unused frame analyzer

## Stato: DONE (2026-07-31)

## Evidenza

`tools/analyze_frames.py` was a standalone OpenCV helper that scanned the
legacy path `build/extracted_output/frame_*.png`. A repository-wide audit found
no active invocations in:

- `.github/workflows/`;
- `CMakeLists.txt`, `cmake/`, or test manifests;
- `tools/`, `tests/`, or `bench/` orchestration;
- developer/CI/WBH gate manifests.

The only non-archive references were the orphan-audit watch entry and
`docs/adr/ADR-014-text-golden-coverage.md`, where the script is explicitly
listed among tools not used by the C++ golden-test path. The active visual
workflow uses `verify_cinematic_showcase.sh` and the C++ golden helpers
instead.

## Change

Deleted `tools/analyze_frames.py`. No replacement was required because its
frame-analysis behavior was not wired into a maintained verification path.
The two related candidates remain intact:

- `tools/render_showcase_contact_sheet.sh` is invoked by
  `.github/workflows/nightly.yml (cinematic-full job)`;
- `tools/selftest_validate_benchmark_json.sh` is the documented five-case
  manual self-test for `tools/validate_benchmark_json.sh`.

## Verification

- Exact basename search outside `docs/ARCHIVE/`: zero executable invocations
  of `analyze_frames.py`.
- CMake, workflow, gate-manifest, and active tool-directory search: zero
  wiring references.
- `ADR-014` confirms the C++ golden path does not depend on the script.
- The deletion is limited to the unused helper plus this ticket and the
  umbrella-audit documentation update.

## Forward points

- `TICKET-TOOLS-ORPHAN-AUDIT` remains open for the other watch-list entries.
- `render_showcase_contact_sheet.sh` must not be removed while the nightly
  workflow invocation remains active.
- `selftest_validate_benchmark_json.sh` may be wired into CI in a future
  benchmark-schema follow-up, but is not unused merely because it is manual.
