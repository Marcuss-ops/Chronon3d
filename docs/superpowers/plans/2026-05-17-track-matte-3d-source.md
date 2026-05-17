# Track Matte 3D Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make matte source layers render with correct 3D camera projection when they are 3D layers, instead of always using a 2D flat silhouette.

**Architecture:** Single-file change in `graph_builder_pipeline.cpp`: replace the `make_2d_item` lambda (which hardcodes `projected=false`) with `make_item_for_matte_source` that runs `project_layer_2_5d()` when `source.is_3d && cam25d.enabled`, mirroring exactly what the regular 3D layer loop does.

**Tech Stack:** C++20, doctest, xxhash, GLM.

---

## File Map

| File | Change |
|------|--------|
| `src/render_graph/builder/graph_builder_pipeline.cpp` | Replace `make_2d_item` lambda; update call site |
| `tests/render_graph/test_track_matte.cpp` | Add 3D matte source test |
| `docs/FUTURE_FEATURES.md` | Mark track matte completato |

---

## Task 1: Fix + Test (TDD)

**Files:**
- Modify: `src/render_graph/builder/graph_builder_pipeline.cpp`
- Modify: `tests/render_graph/test_track_matte.cpp`

### Build command (PowerShell — must set MSVC env)
```powershell
$msvcVersion = "14.50.35717"
$msvcBase = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\$msvcVersion"
$sdkVersion = "10.0.26100.0"
$sdkBase = "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVersion"
$env:INCLUDE = "$msvcBase\include;$sdkBase\ucrt;$sdkBase\um;$sdkBase\shared"
$env:LIB = "$msvcBase\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVersion\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVersion\um\x64"
cmake --build C:\Users\pater\Pyt\Chrono\build\chronon\win-debug -j4
```

### Run tests command
```
ctest --test-dir C:\Users\pater\Pyt\Chrono\build\chronon\win-debug --output-on-failure -R "track_matte|Track matte"
```

---

- [ ] **Step 1: Add failing test to test_track_matte.cpp**

Read the file first. Append this test at the end of `tests/render_graph/test_track_matte.cpp` (after the last `TEST_CASE`):

```cpp
TEST_CASE("Track matte 3D source: projection applies correctly") {
    // A 3D matte circle rotated 60deg around Y produces a narrower silhouette
    // than the same circle unrotated. This verifies the matte source goes through
    // project_layer_2_5d() instead of the 2D flat path.
    auto make_3d_matte_comp = [](float rotate_y) {
        return composition({
            .name = "TrackMatte3D", .width = 320, .height = 200, .duration = 1
        }, [rotate_y](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});

            s.layer("matte_src", [rotate_y](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0}).rotate({0, rotate_y, 0});
                l.circle("c", {.radius = 70.0f, .color = Color::white(), .pos = {}});
            });

            s.layer("target", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0}).track_matte_alpha("matte_src");
                l.rect("r", {.size = {300, 180}, .color = {1, 0, 0, 1}, .pos = {}});
            });

            return s.build();
        });
    };

    auto flat    = test::render_modular(make_3d_matte_comp(0.0f));
    auto rotated = test::render_modular(make_3d_matte_comp(60.0f));
    REQUIRE(flat    != nullptr);
    REQUIRE(rotated != nullptr);

    test::save_debug(*flat,    "output/debug/track_matte/3d_source_flat.png");
    test::save_debug(*rotated, "output/debug/track_matte/3d_source_rotated.png");

    // Y=60deg rotation collapses the matte circle horizontally → different hash
    CHECK(framebuffer_hash(*flat) != framebuffer_hash(*rotated));

    // Count visible (alpha > 0) pixels in the center row for both renders.
    // The rotated matte must be narrower.
    auto count_visible_row = [](const Framebuffer& fb, int y) {
        int count = 0;
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).a > 0.0f) ++count;
        }
        return count;
    };

    const int flat_visible    = count_visible_row(*flat,    100);
    const int rotated_visible = count_visible_row(*rotated, 100);
    CHECK(rotated_visible < flat_visible);
}
```

- [ ] **Step 2: Build and confirm test FAILS (or doesn't compile)**

```powershell
# Set env first (see build command above), then:
cmake --build C:\Users\pater\Pyt\Chrono\build\chronon\win-debug -j4
ctest --test-dir C:\Users\pater\Pyt\Chrono\build\chronon\win-debug --output-on-failure -R "3D source"
```

Expected: test compiles but FAILS — `rotated_visible < flat_visible` is false because the 2D path ignores rotation.

If the test unexpectedly passes, do NOT proceed. Report NEEDS_CONTEXT — the existing code may already handle this case.

- [ ] **Step 3: Implement fix in graph_builder_pipeline.cpp**

Read the file. Find the `make_2d_item` lambda (around line 55-65). Replace it AND update the call site.

**Replace the `make_2d_item` lambda (lines 55-65) with `make_item_for_matte_source`:**

```cpp
    // Helper: build a LayerGraphItem for a matte source layer.
    // If the source is a 3D layer and the camera is active, apply the full
    // 2.5D projection so the matte silhouette matches the projected layer geometry.
    auto make_item_for_matte_source = [&](const ResolvedLayer& rl) -> LayerGraphItem {
        if (cam25d.enabled && rl.layer->is_3d) {
            Transform effective_transform = rl.world_transform;
            if (!ctx.modular_coordinates) {
                effective_transform.position.x -= ctx.width * 0.5f;
                effective_transform.position.y -= ctx.height * 0.5f;
            }
            const Mat4 projection_world_matrix = effective_transform.to_mat4();
            auto proj = project_layer_2_5d(
                effective_transform,
                projection_world_matrix,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height)
            );
            if (proj.visible) {
                const Mat4 eff_proj = is_native_3d_layer(*rl.layer)
                    ? Mat4(1.0f)
                    : proj.projection_matrix;
                return LayerGraphItem{
                    .layer             = rl.layer,
                    .transform         = proj.transform,
                    .world_matrix      = rl.world_matrix,
                    .projection_matrix = eff_proj,
                    .depth             = proj.depth,
                    .world_z           = rl.world_transform.position.z,
                    .projected         = true,
                    .native_3d         = is_native_3d_layer(*rl.layer),
                    .insertion_index   = rl.insertion_index,
                };
            }
        }
        // 2D layer or camera disabled: flat path
        return LayerGraphItem{
            .layer           = rl.layer,
            .transform       = rl.world_transform,
            .world_matrix    = rl.world_matrix,
            .depth           = 0.0f,
            .world_z         = rl.world_transform.position.z,
            .projected       = false,
            .native_3d       = is_native_3d_layer(*rl.layer),
            .insertion_index = rl.insertion_index,
        };
    };
```

**Update the call site inside `append_item` (was line 74):**

Change:
```cpp
                LayerGraphItem matte_item = make_2d_item(*it->second);
```
To:
```cpp
                LayerGraphItem matte_item = make_item_for_matte_source(*it->second);
```

- [ ] **Step 4: Build and run all track_matte tests**

```powershell
cmake --build C:\Users\pater\Pyt\Chrono\build\chronon\win-debug -j4
ctest --test-dir C:\Users\pater\Pyt\Chrono\build\chronon\win-debug --output-on-failure -R "track_matte|Track matte"
```

Expected: ALL track matte tests pass (including the new 3D source test).

If the new test still fails (`rotated_visible < flat_visible` false), check the PNG outputs in `output/debug/track_matte/` — the rotated image should visually show a narrower circle than the flat one.

- [ ] **Step 5: Run full test suite to check regressions**

```
ctest --test-dir C:\Users\pater\Pyt\Chrono\build\chronon\win-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
cd C:\Users\pater\Pyt\Chrono
git add src/render_graph/builder/graph_builder_pipeline.cpp \
        tests/render_graph/test_track_matte.cpp
git commit -m "fix(track-matte): apply 3D projection to matte source layers

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 2: Update docs

**Files:**
- Modify: `docs/FUTURE_FEATURES.md`

- [ ] **Step 1: Mark track matte completato**

In `docs/FUTURE_FEATURES.md`, find line:
```
| **Track matte** | ✅ alpha/luma/inverted per layer 2D verificati; source 3D da follow-up. | quasi completo |
```

Replace with:
```
| **Track matte** | ✅ alpha/luma/inverted + source 3D proiettato correttamente. 3D matte source usa `project_layer_2_5d()` — silhouette segue camera projection. | completato |
```

- [ ] **Step 2: Commit**

```bash
cd C:\Users\pater\Pyt\Chrono
git add docs/FUTURE_FEATURES.md
git commit -m "docs: mark track matte 3D source complete

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Self-Review

- [x] Spec coverage: fix in pipeline + test + docs — all three covered
- [x] No placeholders: all code is complete and real
- [x] Type consistency: `LayerGraphItem`, `ResolvedLayer`, `project_layer_2_5d`, `is_native_3d_layer` — all names from the actual codebase
- [x] The new lambda mirrors exactly the 3D bin loop (lines 124-157 in pipeline.cpp) — no divergence
