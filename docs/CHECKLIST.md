# Checklist — Final Validation

> **core only builds, adapters only call, data only flows in.**

Protocol to be executed before every merge or release to verify that the repository is consistent, compilable, and free of conceptual duplications.

---

## 1. Full Build

```bash
# General build command (adjust path to your build directory)
cmake --build build --target chronon3d_cli chronon3d_tests -j $(nproc)
```

**Expected Outcome:**
- No compilation errors
- No new blocking warnings
- Binaries generated correctly
- No linker errors

---

## 2. Automated Tests

```bash
# Run tests using ctest
ctest --test-dir build --output-on-failure
```

**Expected Outcome:**
- `chronon3d_tests` passes everything
- No failed tests, no crashes

---

## 3. Render Single Frame (specscene)

```bash
# Verify CLI rendering
./build/apps/chronon3d_cli/chronon3d_cli render input_file.specscene -o output/test_render.png --frames 0
```

**Expected Outcome:**
- PNG created at the specified path
- Output consistent with the input specification
- No visual artifacts or missing elements

---

## 4. Clean Git Status

```bash
git status --short
```

**Expected Outcome:**
- No unwanted files
- Only the changes intended to be kept
- No build artifacts or accidentally generated files

---

## 5. Check for Conceptual Duplications

```bash
rg -n "templates|visual_presets|rendergraph|examples/proofs" include src tests docs
```

**Expected Outcome:**
- No old layers re-emerging
- No logic duplicated under new names
- No legacy structures becoming a second parallel logic

---

## Summary: "Final OK"

| # | Check | Outcome |
|---|---|---|
| 1 | Build passes | ✅ / ❌ |
| 2 | Tests pass | ✅ / ❌ |
| 3 | CLI render correct | ✅ / ❌ |
| 4 | `git status` clean | ✅ / ❌ |
| 5 | No duplicates (`rg`) | ✅ / ❌ |

All ✅ → ready for release.
