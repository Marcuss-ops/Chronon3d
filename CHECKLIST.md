# Checklist — Final Validation

> **core only builds, adapters only call, data only flows in.**

Protocol to be executed before every merge or release to verify that the repository is consistent, compilable, and free of conceptual duplications.

---

## 1. Full Build

```powershell
cmake --build build\chronon\win-release --target chronon3d_cli chronon3d_tests -j 4
```

**Expected Outcome:**
- No compilation errors
- No new blocking warnings
- Binaries generated correctly
- No linker errors

---

## 2. Automated Tests

```powershell
ctest --test-dir build\chronon\win-release -C Release --output-on-failure
```

**Expected Outcome:**
- `chronon3d_tests` passes everything
- No failed tests, no crashes

---

## 3. Render Single Frame (specscene)

```powershell
build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe render output\background_preview.specscene -o output\background_preview.png --frames 0
```

**Expected Outcome:**
- PNG created at `output/background_preview.png`
- Dark background present
- Grid visible
- No text, no badges, no extra elements not requested

---

## 4. Full HD Render (1920×1080)

```powershell
build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe render output\background_preview_1920x1080.specscene -o output\background_preview_1920x1080.png --frames 0
```

**Expected Outcome:**
- PNG 1920×1080 created
- Regular grid
- Composition consistent with the specscene file

---

## 5. Clean Git Status

```powershell
git status --short
```

**Expected Outcome:**
- No unwanted files
- Only the changes intended to be kept
- No build artifacts or accidentally generated files

---

## 6. Check for Conceptual Duplications

```powershell
rg -n "templates|visual_presets|rendergraph|examples/proofs" include src tests docs Operations
```

**Expected Outcome:**
- No old layers re-emerging
- No logic duplicated under new names
- No `templates` / `presets` / `examples` becoming a second parallel logic

---

## Summary: "Final OK"

| # | Check | Outcome |
|---|---|---|
| 1 | Build passes | ✅ / ❌ |
| 2 | Tests pass | ✅ / ❌ |
| 3 | Specscene PNG render correct | ✅ / ❌ |
| 4 | Full HD render consistent | ✅ / ❌ |
| 5 | `git status` clean | ✅ / ❌ |
| 6 | No duplicates (`rg`) | ✅ / ❌ |

All ✅ → ready for release.
