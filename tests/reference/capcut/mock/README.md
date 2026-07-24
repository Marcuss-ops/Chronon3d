# Chronon3D mock recreation corpus

This corpus recreates the ten visual examples downloaded from the supplied
Drive folder. It is intentionally separate from `reference/`: the source
images contain the watermark `UNBLESSED MOCK — TEST ONLY — NOT A CAPCUT
REFERENCE`, are RGB `1672×941`, and are not authentic CapCut exports.

Generate the real Chronon3D renders with:

```bash
CHRONON3D_CAPCUT_MANIFEST=tests/reference/capcut/mock/manifest.json \
CHRONON3D_CAPCUT_CORPUS_ROOT=tests/reference/capcut/mock \
CHRONON3D_CAPCUT_RENDER_CURRENT=1 \
build/chronon/linux-ci-release-build/tests/chronon3d_capcut_parity_tests
```

The resulting files under `current/` are product renders for visual review;
they must never be promoted to the blessed CapCut corpus automatically.
