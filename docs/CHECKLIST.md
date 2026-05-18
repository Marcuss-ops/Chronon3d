# Checklist — Final Validation

> **core only builds, adapters only call, data only flows in.**

Protocol to be executed before every merge or release to verify that the repository is consistent, compilable, and free of conceptual duplications.

---

## 1. Full Build

Configure and build the project using CMake presets:

```bash
# On Linux
cmake --preset linux-release
cmake --build --preset linux

# On Windows
cmake --preset win-release
cmake --build --preset win
```

**Expected Outcome:**
- No compilation errors
- No new blocking warnings
- Binaries generated correctly (`chronon3d_cli` and test suites)
- No linker errors

---

## 2. Automated Tests

Run the complete test suite using CTest presets:

```bash
# On Linux
ctest --preset linux-test --output-on-failure

# On Windows
ctest --preset win-test --output-on-failure
```

**Expected Outcome:**
- All tests in all test suites (`chronon3d_core_tests`, `chronon3d_scene_tests`, `chronon3d_renderer_tests`, `chronon3d_io_tests`, `chronon3d_cli_tests`) pass
- No failed tests, no crashes

---

## 3. Render Single Frame

Verify CLI rendering by running a sample composition render:

```bash
# On Linux
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render MyComp --frame 0 -o output/test_render.png

# On Windows
.\build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe render MyComp --frame 0 -o output/test_render.png
```

**Expected Outcome:**
- PNG created at the specified path (`output/test_render.png`)
- Output consistent with the composition specification
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
