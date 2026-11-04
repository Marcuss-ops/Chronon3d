# Golden E2E gate

Three deterministic software renders cover the Pareto production path:

| Plan | Covers |
|---|---|
| `01_editorial_text` | shaped text over an image layer |
| `02_packshot_2p5d` | two depth-ordered image planes plus a caption |
| `03_mixed_mesh` | inline mesh, depth-ordered plane and a caption |

`run_golden_e2e.py` renders each plan twice through the real CLI with
`--video-sink raw`, hashes every RGBA frame with SHA-256 and compares the
result with `goldens.v1.json`. It fails on a changed pixel, a size drift, a
missing frame or a non-deterministic render.

Run it manually:

```bash
python3 tests/e2e/golden/run_golden_e2e.py \
  --cli build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
```

CTest target: `chronon3d_golden_e2e_tests` (labels `e2e;golden`).

## Updating goldens

Only after an intentional visual change, with the evidence in the same commit:

```bash
python3 tests/e2e/golden/run_golden_e2e.py --cli <cli> --update-goldens
```

Do not regenerate to make an unexplained failure pass. The manifest is
reviewed like code: a hash change is a visual change.
