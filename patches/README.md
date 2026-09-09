# Patches — FIXING.md follow-up (require build + tests)

> **Do NOT apply without CI.** These patches are Medium-risk changes that
> touch hot-path, hashing or error-code semantics. Each patch header states
> its risk and required verification.

| Patch | Finding | Risk | What it does |
|---|---|---|---|
| `C-03-unify-double-sha256.patch` | C-03 | **Medium** | Unify double `sha256_file` in `ResourcePreparation` (single I/O per image) |
| `C-04-normalize-font-size.patch` | C-04 | **Medium** | `GpuGlyphKey::make()` normalizes `font_size=0` for distance-field reps |
| `H-02-xxhash-glyph-key.patch` | H-02 | **Medium** | Replace `ostringstream+sha256` glyph key with `xxHash` view (zero alloc) |
| `E-06-preserve-asset-mount-failed.patch` | E-06 | **Low** | Preserve `AssetMountFailed` in `RuntimeContext::create` instead of `InternalError` |

**How to apply:**

```bash
# Dry-run first
git apply --check patches/C-03-unify-double-sha256.patch
# Apply
git apply patches/C-03-unify-double-sha256.patch
# Build + targeted tests (example)
cmake --preset linux-fast-dev && cmake --build --preset linux-fast-dev
ctest --test-dir build/fast --output-on-failure -R "resource_preparation|runtime|glyph"
```

**Verification per patch:**

- **C-03:** `tests` for `ResourcePreparation` + manual: job with 10 images still produces identical `content_digest` (no double read).
- **C-04:** `tests/text/*` + `tests/backends/text/*` — Mtsdf glyphs with different `font_size` must map to same atlas entry.
- **H-02:** Benchmark: no regression in `bench_scene_program`; heap alloc per glyph goes to 0 on large-glyph path. Requires adapting `digest_from_u64` to your `ContentDigest` type.
- **E-06:** `RuntimeContext::create` with bad `assets_root` returns `Code::AssetMountFailed` not `InternalError`.

**Already applied (no build needed, on `main`):**

- D-01 duplicate include, D-04 CMake comment, H-07 `download_scratch_` doc, U-01 hash TODO, `docs/GOVERNANCE.md`

See `FIXING.md` for full audit context and phased remediation plan.
