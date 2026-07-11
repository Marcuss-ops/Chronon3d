# Pipeline Parity Real — Regression Golden

This directory is reserved for the **PNG-hash-of-hash regression golden**
that locks the byte-exact output of the pipeline-parity real test
(`tests/text/test_pipeline_parity_real.cpp`) against unintended drift.

## Status

**Placeholder — to be generated once the test stabilises.**

The pipeline-parity real test currently asserts SDK-still == CLI-still
via `framebuffer_hash` (XXH64 over the rendered PNG pixels). The
regression golden will add a secondary, cross-pipeline lock:

  1. For each variant (default / `--no-graph` / `--graph` /
     `--diagnostic-overlay` / `--diagnostic-overlay-only`), capture
     the expected `framebuffer_hash` of the canary render.
  2. Hash the table of expected hashes (the "hash-of-hash") and store
     it as a single golden value.
  3. Render the canary once and verify the hash-of-hash matches —
     catching drift that escapes the pairwise SDK-vs-CLI comparison
     (e.g. a uniform re-shading that cancels in the pairwise check).

## Planned artefacts (TBD)

  * `golden_hashes.txt` — table of variant → expected `framebuffer_hash`.
  * `golden_hash_of_hash.txt` — XXH64 of the table above (single 64-bit value).
  * `pipeline_parity_canary.png` — reference PNG of the canary render
    for visual diffing / future PR-review attachment.

## Generation

Once the test is green across all variants on a clean `main` build, run
the test suite with the golden-update env var (the exact knob will be
introduced alongside the golden test case):

```bash
CHRONON3D_UPDATE_GOLDENS=1 ctest -R pipeline_parity_real
```

## Related

  * Test: `tests/text/test_pipeline_parity_real.cpp`
  * Test cmake: `tests/pipeline_parity_real_tests.cmake`
  * Canary: `tests/text/pipeline_parity_canary.hpp`
