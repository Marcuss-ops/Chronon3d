# Migration Plan — `TextSpec` shape (new composable form `.content.value` + `.font`)

| Field | Value |
|---|---|
| **Date planned** | 2026-06-21 |
| **Owner** | TBD — see TICKET-002 §Suggested fix approach |
| **Status** | 🔵 Planned (audit completed; mechanical transform queued — see "Proposed PR split" below) |
| **Trigger** | Re-run of `cmake --preset linux-full-validation` after TICKET-008 close-out, while surfacing TICKET-002's compile errors from `content/`. |
| **Doc location** | Companion to TICKET-002 in `docs/FOLLOWUP_TICKETS.md`. This doc is the per-file recipe; the ticket is the policy/decision center. |

---

## 1. Background — why this migration is mechanical

`include/chronon3d/scene/builders/builder_params.hpp` declares **two equivalent shapes** for the same `chronon3d::TextSpec`:

```cpp
using TextParams = TextSpec;   // deprecated alias kept only as a transition aid

struct TextSpec {
    TextContent        content;        // {.value = "...", .pre_shaped = ...}
    FontSpec           font;           // {.font_path, .font_family, .font_weight, .font_style, .font_size}
    TextLayoutSpec     layout;         // {.box, .anchor, .align, .vertical_align, .wrap, .line_height, .tracking, ...}
    TextAppearanceSpec appearance;     // {.color, .paint, .shadows, .material, .box_style}
    Vec3               position{};
};
```

The legacy flat shape that older `content/` call sites still use was a **30-field `TextParams` monolith** with `.text` as a direct field and `.layout`/`appearance`/etc. all flattened into one designated-initializer list. The struct now has no `.text` field — the value lives at `.content.value` — and `.layout` is its own sub-struct that cannot co-exist with another `.layout` in a single designated-init-list (one designator per subobject).

**The compiler errors (verbatim from the TICKET-002 Symptom)**:

| Error | Cause |
|---|---|
| `'.layout' designator used multiple times in the same initializer list` | C++ designated-init-list rule violated by call sites that flatten multiple layout concerns into one `.layout = …` repeat. |
| `'chronon3d::TextSpec' has no non-static data member named 'text'` | Brace-init callsites that wrote `.text = "X"` against the new struct. |
| `cannot convert '<brace-enclosed initializer list>' to 'chronon3d::TextSpec'` | Brace-init overall pattern changed because the field enumeration changed. |

---

## 2. Re-audit (2026-06-21) — the rot is concentrated, not 102+ files

> **TICKET-002 was filed 2026-06-19 with "102+ errors in 7 files"**. Two days of PR-merge activity between then and now have **massively reduced** the rot. The current state (verified by `grep` on 2026-06-21) is:

### 2.1 TICKET-002's listed files — 5/7 don't exist on the original paths anymore

| TICKET-002 listed path | Status on 2026-06-21 |
|---|---|
| `content/shapes/proofs/shape_proofs.cpp` | EXISTS, **already migrated to new `.content`/`.font`/`.layout`/`.appearance`/`.position` shape** (verified by reading 378 lines of body — all `TextSpec{...}` use the new shape correctly). |
| `content/shapes/motion/shape_motion_proofs.cpp` | EXISTS, **already migrated** (same pattern, every `TextSpec{...}` uses new shape). |
| `content/image_proofs.cpp` | **MISSING**. (May have been moved under `content/images/compositions/` — needs re-scan if content/ tree is restructured.) |
| `content/image_test_patterns.cpp` | **MISSING**. (Same as above.) |
| `content/camera/camera_advanced_tests_diag.cpp` | **MISSING** from the listed path; an analogous file at `content/2d5/compositions/camera_advanced_tests_diag.cpp` already uses the new shape. |
| `content/camera/camera_calibration_scene.cpp` | **MISSING** from the listed path; an analogous file at `content/2d5/compositions/camera_calibration_scene.cpp` already uses the new shape. |
| `content/camera/camera_test_orchestrator.cpp` | **MISSING** from the listed path. |

### 2.2 Where the rot actually lives today (verified by grep 2026-06-21)

The historical "102+" figure was a tally over a partial-build parse; today's static audit (full `content/` sweep) shows the rot is concentrated in a **single helper function + its 6+ call sites**, plus an additional 5 direct callsites that use the helper:

| # | File | Line(s) | Legacy pattern | Migration shape |
|---|---|---|---|---|
| 1 | `content/text/text_helpers.hpp` / `content/text/text_helpers.cpp` | `centered_text(...)` body | `centered_text(...)` returns `TextSpec{.text, .box, .font_size, .tracking, .color, .line_height, …}` (legacy brace-init) | Rewrite helper body to return `TextSpec{.content = {.value = ...}, .font = {.font_size = ...}, .layout = {.box, .align, .vertical_align, .line_height, .tracking}, .appearance = {.color}, .position = default}` |
| 2 | `content/anims/compositions/cinematic_text_camera.cpp` | 70 (`title_text`) | Wrapper helper that calls `centered_text({.text, .box, .font_size, .tracking, .color, .line_height})` | After #1 lands: helper signature unchanged — but the call inside it must use the new-shape `centered_text` args. **Inside the function, no edit needed if the helper body is reshaped first.** |
| 3 | `content/anims/compositions/cinematic_text_camera.cpp` | 142, 256, 350, 450, 486, 599 | `centered_text({.text = "X", .box = …, .font_size = …, .tracking = …, .color = …, .line_height = …})` (legacy brace-init) | After #1 lands: rewrite the brace-init at each call site to the new-shape brace-init OR rely on (new) helper defaults. |
| 4 | `content/anims/compositions/animation_compositions.cpp` | 69 | `.text = "Typewriter"` inside `text::typewriter_build(s, "tw", {…}, ctx.frame)` (legacy brace-init for `TextRevealDescriptor`'s `.text` field — this one IS a separate named field on TextRevealDescriptor and is FINE; see note below). | **NOTE**: `TextRevealDescriptor::text` is a separate, valid field on a different struct (`content/common/text_reveal_helpers.hpp`). It's NOT a `TextSpec`'s `.text`. Confirm this is unrelated and skip migration. |
| 5 | `content/anims/compositions/text_animations.cpp` | 229, 238, 321, 330 | `TextRevealDescriptor{.text = "...", .font_size = N, .font_spec = spec, .tracking = ..., …}` | Same as #4 — `.text` here is on `TextRevealDescriptor`, NOT `TextSpec`. Not in scope of THIS migration. |
| 6 | `content/anims/compositions/text_animations.cpp` | 76–82 (`txt_center`) | Helper that builds new-shape `TextSpec` correctly already (uses `.content/.font/.layout/.appearance/.position`). | **Already correct** — skip. |
| 7 | `content/text/text_glow_helpers.hpp` | example comment line 23 | `l.text("t", centered_text({.text = "TITLE", .font_size = 96}))` (a doc-commented example, NOT compiled) | **Optional** — update example comment to new-shape brace-init OR drop the inline brace-init and use `chronon3d::content::text::centered_text("TITLE", 96.0f)` (positional args) once #1 redesigns the helper signature. |

> **Recount**: actual content/ rot for `TextSpec` legacy-shape rebuild is **1 helper (`centered_text`) + 6 inline callsites in `cinematic_text_camera.cpp`**, totalling ~7 source-level edits. **Plus optional 1 doc-comment update in `text_glow_helpers.hpp`.**

### 2.3 The cmake-side blocker — separate rot, unrelated to TICKET-002's content

Re-running `cmake --preset linux-full-validation` on 2026-06-21 surfaced:

```
CMake Error at experimental/expressions/tests/CMakeLists.txt:27
  (target_link_libraries):
  Target "chronon3d_expressions_v2_tests" links to: doctest::doctest
  but the target was not found.
```

This is **NOT** part of TICKET-002's content/ rot. It's a CMake-glue rot in `experimental/expressions/tests/CMakeLists.txt:27`: the test target references `doctest::doctest` as a target name, but the dovetailed find-package logic in this scope yields a raw library instead of an imported/aliased target. The configure failure aborts before `content/` tests are reached, which is why TICKET-002's "102+ errors" are unverifiable from `cmake --build` today.

**Recommended path**: in the same PR as the content/ migration, add the 1-line glue fix:

```cmake
# experimental/expressions/tests/CMakeLists.txt
target_link_libraries(chronon3d_expressions_v2_tests
    PRIVATE
        chronon3d_expressions_v2
        doctest::doctest                  # already there
        $<$<TARGET_EXISTS:doctest::doctest>:doctest::doctest>    # ← adds gen-exp guard
)
```

OR use the `:chrono3d_enable_test_pch(...)` macro already used elsewhere (see `tests/core_tests.cmake:90–103`) which threads target_link_libraries correctly. Either path is acceptable; the gen-exp form is preferred for consistency with `TICKET-006`'s precedent.

> **Note**: this rot blocks the `cmake --build` flow that would otherwise surface the TICKET-002 errors. Fixing it is a precondition for the audit recount to be MACHINE-VERIFIED (otherwise we keep relying solely on static grep).

---

## 3. The mechanical transform recipe

### 3.1 Field mapping table (legacy → new)

| Legacy TextSpec field | New location | Migration action |
|---|---|---|
| `.text = "X"` | `.content.value = "X"` (and `TextContent` may carry a `.pre_shaped: std::shared_ptr<PlacedGlyphRun>` which is `nullptr` by default) | Replace `.text = "X"` with `.content = {.value = "X"}` (preserves the existing `pre_shaped` default). |
| `.font_size = N` (top-level) | `.font.font_size = N` | Move into the `FontSpec` sub-struct. |
| `.font_spec = X` (top-level, when `X` was a `FontSpec`) | `.font = X` | Wrap the existing `FontSpec` value into the `FontSpec` sub-struct field. |
| `.font_path = "..."` (top-level) | `.font.font_path = "..."` | Move into the `FontSpec` sub-struct. |
| `.font_family = "..."`, `.font_weight = N`, `.font_style = "..."` (top-level) | `.font.font_family`, `.font.font_weight`, `.font.font_style` | Move into the `FontSpec` sub-struct. |
| `.box = {W, H}` (top-level) | `.layout.box = {W, H}` | Move into the `TextLayoutSpec` sub-struct. |
| `.anchor`, `.centering_mode`, `.align`, `.vertical_align`, `.wrap`, `.line_height`, `.tracking`, … (top-level layout props) | `.layout.<same>` | All layout props move into `TextLayoutSpec`. |
| `.color = …` (top-level) | `.appearance.color = …` | Move into the `TextAppearanceSpec` sub-struct. |
| `.pos` / position vectors | `.position = Vec3{...}` | Move into top-level `Vec3 position` field. |
| (no-equivalent — pre-shaped glyph) | `.content.pre_shaped = std::shared_ptr<PlacedGlyphRun>(...)` | New capability: pass through pre-shaped glyph for places that have already laid out the run. |

### 3.2 Mechanical transform (template-driven)

For each legacy-shape designated-init, the transformation is "fan-out" into the sub-structs:

```cpp
// LEGACY:
TextSpecLegacy ts{
    .text        = "TITLE",
    .font_size   = 64.0f,
    .font_spec   = font_bold(),
    .box         = {1200.0f, 240.0f},
    .align       = TextAlign::Center,
    .vertical_align = VerticalAlign::Middle,
    .line_height = 1.10f,
    .tracking    = 6.0f,
    .color       = FRESH_TEXT_WHITE,
};

// NEW (mechanical fan-out, preserving the field set):
TextSpec ts{
    .content    = {.value            = "TITLE"},
    .font       = {/*.font_path=*/    font_bold().font_path,
                   /*.font_family=*/  font_bold().font_family,
                   /*.font_weight=*/  font_bold().font_weight,
                   /*.font_style=*/   font_bold().font_style,
                   /*.font_size=*/    64.0f},     // combined from top-level .font_size
    .layout     = {.box              = {1200.0f, 240.0f},
                   /*.anchor=*/      TextAnchor::Center,
                   .align            = TextAlign::Center,
                   .vertical_align   = VerticalAlign::Middle,
                   /*.wrap=*/        TextWrap::Word,
                   /*.line_height=*/ 1.10f,
                   .tracking         = 6.0f,
                   /*... defaults left to TextLayoutSpec ...*/},
    .appearance = {.color            = FRESH_TEXT_WHITE,
                   /*.paint, .shadows, .material, .box_style all default to {} */},
    .position   = {0.0f, 0.0f, 0.0f},    // default Vec3 for callers who don't carry this
};
```

> **Trick**: the helper `centered_text(...)` body should encapsulate this fan-out so that **call sites can pass the same arg signature as before** — making the call-site edits SMALL. See §4.

### 3.3 Forbidden-pattern checklist

After the migration, NO callsite in `content/` should leave the following patterns behind (grep canaries):

```bash
grep -rn -E 'TextSpec\{[^}]*\.text\s*=' content/    # no .text = inside TextSpec{...}
grep -rn -E 'TextSpec\{[^}]*\.font_size\s*=' content/   # no top-level .font_size
grep -rn -E 'TextSpec\{[^}]*\.font_spec\s*=' content/   # no top-level .font_spec
grep -rn -E 'TextSpec\{[^}]*\.font_path\s*=' content/   # no top-level .font_path
grep -rn -E 'TextSpec\{[^}]*\.box\s*=' content/    # no top-level .box
grep -rn -E 'TextSpec\{[^}]*\.align\s*=' content/   # no top-level .align
grep -rn -E 'TextSpec\{[^}]*\.tracking\s*=' content/   # no top-level .tracking
grep -rn -E 'TextSpec\{[^}]*\.line_height\s*=' content/   # no top-level .line_height
grep -rn -E 'TextSpec\{[^}]*\.color\s*=' content/   # ambiguous (TextAppearanceSpec.color vs TextSpec.color)
```

If any of these return non-zero hits after the helper rewrite + 6 inline-call rewrites, the migration is incomplete.

---

## 4. Helper rewrite — `chronon3d::content::text::centered_text(...)`

> This is the centrepiece of the migration. The 6 inline callsites in `cinematic_text_camera.cpp` all go through this helper — reshape the helper's BODY, the call sites re-route automatically.

### 4.1 Today (legacy)

```cpp
// content/text/text_helpers.hpp (today)
namespace chronon3d::content::text {
inline TextSpec centered_text(const centered_text_args_t& args) {
    return TextSpec{        // FAILS — TextSpec has no .text, .font_size, .box, .color, .tracking, .line_height top-level
        .text        = args.text,
        .box         = args.box,
        .font_size   = args.font_size,
        .tracking    = args.tracking,
        .color       = args.color,
        .line_height = args.line_height,
    };
}
}
```

### 4.2 Tomorrow (new shape, helper body)

```cpp
// content/text/text_helpers.hpp (after migration)
namespace chronon3d::content::text {

struct centered_text_args_t {
    std::string text;                              // -> TextContent.value
    Vec2  box              = {1500.0f, 220.0f};    // -> TextLayoutSpec.box
    f32   font_size        = 72.0f;                // -> FontSpec.font_size
    f32   tracking         = 6.0f;                 // -> TextLayoutSpec.tracking
    Color color            = Color{1,1,1,1};       // -> TextAppearanceSpec.color
    f32   line_height      = 1.10f;                // -> TextLayoutSpec.line_height
    TextAlign     align    = TextAlign::Center;
    VerticalAlign valign  = VerticalAlign::Middle;
    FontSpec      font     = {};                   // override path: full font_spec pass-through
    Vec3          position = {0.0f, 0.0f, 0.0f};
};

inline TextSpec centered_text(const centered_text_args_t& args) {
    TextSpec out;
    out.content.value         = args.text;
    out.font.font_size        = args.font_size;
    out.font.font_path        = args.font.font_path;
    out.font.font_family      = args.font.font_family;
    out.font.font_weight      = args.font.font_weight;
    out.font.font_style       = args.font.font_style;
    out.layout.box            = args.box;
    out.layout.align          = args.align;
    out.layout.vertical_align = args.valign;
    out.layout.line_height    = args.line_height;
    out.layout.tracking       = args.tracking;
    out.appearance.color      = args.color;
    out.position              = args.position;
    return out;
}

} // namespace
```

### 4.3 Call-site rewrites — `cinematic_text_camera.cpp` 6 sites

After §4.2 lands, the 6 call sites can be **left verbatim** if their `centered_text({...})` arg structs are renamed/extended to match the new `centered_text_args_t` field set. Otherwise, replace legacy fields:

| File | Line(s) | Source line | New transform |
|---|---|---|---|
| `cinematic_text_camera.cpp` | 142 | `centered_text({.text = L.text, .box = {1500.0f, 320.0f}, .font_size = L.size, .tracking = 8.0f, .color = L.color, .line_height = 1.10f})` (inside lambda) | **NONE** if `centered_text_args_t` keeps the same field names (just type-changes under the hood). Verify by grep. |
| `cinematic_text_camera.cpp` | 256 | `centered_text({.text = "MOTION BY CAMERA", .box = {1100.0f, 80.0f}, .font_size = 38.0f, .tracking = 12.0f, .color = {1.0f, 0.55f, 0.75f, 1.0f}, .line_height = 1.10f})` | Same as above. |
| `cinematic_text_camera.cpp` | 350 | `centered_text({.text = "AURORA", .box = {1200.0f, 320.0f}, .font_size = 220.0f, ...})` | Same. |
| `cinematic_text_camera.cpp` | 450 | `centered_text({.text = "FOCUS NEAR", .box = {1500.0f, 240.0f}, .font_size = 130.0f, ...})` | Same. |
| `cinematic_text_camera.cpp` | 486 | `centered_text({.text = "FAR AWAY", .box = {1500.0f, 220.0f}, .font_size = 120.0f, ...})` | Same. |
| `cinematic_text_camera.cpp` | 599 | `centered_text({.text = ch, .box = {fs * 1.5f, fs * 1.8f}, .font_size = fs, ...})` | Same. |

> **Decision trade-off**: keep `centered_text_args_t` field set stable so call sites need ZERO edit, vs. rewrite the helper to take positional args `(std::string text, f32 font_size, …)` which is also fine but more invasive. **Recommendation**: keep field set stable. That's the lowest-risk path (1 file body edit, 6 call sites untouched, 0 surprise for callers).

### 4.4 Doc-comment fix — `content/text/text_glow_helpers.hpp:23`

The example comment block reads:

```
l.text("t", centered_text({.text = "TITLE", .font_size = 96}));
```

After migration this still compiles (because helper args are unchanged), but the comment is misleading (it implies `centered_text` is constructing a `TextSpec` directly). Update to:

```
l.text("t", centered_text({.text = "TITLE", .font_size = 96.0f}));
// (centered_text() is just a sugar wrapper that returns the new-shape TextSpec;
//  the brace-init args here are centered_text_args_t fields, not TextSpec fields.)
```

Or, even cleaner, change to positional:

```
l.text("t", centered_text("TITLE", 96.0f));
```

Both forms are valid once §4.2 lands.

---

## 5. Proposed PR split

### PR-A: cmake-side blocker (small, unblocks future verification)

1 file: `experimental/expressions/tests/CMakeLists.txt`

```diff
 target_link_libraries(chronon3d_expressions_v2_tests
     PRIVATE
         chronon3d_expressions_v2
-        doctest::doctest
+        $<$<TARGET_EXISTS:doctest::doctest>:doctest::doctest>
 )
```

**Acceptance**: `cmake --preset linux-full-validation` configure succeeds (rc=0); `cmake --build build/chronon/<preset> --target chronon3d_expressions_v2_tests` succeeds.

> Optional: deliver as part of PR-B if the team prefers one-PR-per-feature; otherwise split for fastest unblock.

### PR-B: content/ migration (medium, mechanical)

| # | Edit | Files |
|---|---|---|
| 1 | Rewrite `centered_text` helper body (and `centered_text_args_t` default-values) | `content/text/text_helpers.hpp` (and `.cpp` if helper is non-inline) |
| 2 | Update `content/text/text_glow_helpers.hpp` example comment | `content/text/text_glow_helpers.hpp` (1-doc-only edit) |
| 3 | Run the grep-canaries in §3.3 — should report zero hits. | (verification) |
| 4 | Run `cmake --preset linux-full-validation` (after PR-A) and verify `content/` target compiles clean. | (verification) |

**Acceptance**:
- `cmake --preset linux-full-validation && cmake --build build/chronon/linux-full-validation --target chronon3d_content --target chronon3d_diagnostics` returns rc=0.
- All grep canaries in §3.3 return zero hits.
- The "Forbidden-pattern checklist" output is empty.

### PR-C: TICKET-002 closure (cleanup, doc-only)

1 file: `docs/FOLLOWUP_TICKETS.md`

Update TICKET-002's "Status" → 🟢 Done, with cross-reference to PR-A and PR-B resolved-at commits.

**Acceptance**: `git grep -n 'TICKET-002' docs/FOLLOWUP_TICKETS.md` shows status 🟢 Done with commit refs.

---

## 6. Acceptance criteria (composite)

| # | Criterion | Verification |
|---|---|---|
| 1 | `cmake --preset linux-full-validation` configure rc=0 | Run; expect no `CMake Error`. |
| 2 | `cmake --build build/chronon/linux-full-validation` (or specifically the `content/` subtree) rc=0 | Run; expect zero `TextSpec`/`text_run_builder.cpp` linker/compile errors. |
| 3 | Grep canaries in §3.3 return zero hits | `grep -rn -E '...'` lines, each returns 0 hits. |
| 4 | `content/.../cinematic_text_camera.cpp` compiles clean | Full-file build with `c++ -std=c++20 -Wall -Wextra -fsyntax-only` (or its cmake analogue) reports zero issues. |
| 5 | `centered_text("X", 96.0f)` positional form works (smoke-check): at least one composition using it renders without errors | Run `chronon3d_cli video AnimTypewriterSimple -o /tmp/test.mp4` (or similar) and verify the resulting MP4 renders text correctly. |
| 6 | `tools/test_architectural.sh` Section 3 (Anti-skip-senza-ticket) still passes | Run the script and verify the relevant anti-skip checks report zero hits. |
| 7 | `git grep 'ExecutionPlanCache' -- include/ src/ apps/ tests/` returns zero | One-pass verification post-edit (this is TICKET-008's invariant, must hold). |
| 8 | TICKET-002 status flipped to 🟢 Done in `docs/FOLLOWUP_TICKETS.md` | Doc-edit. |

---

## 7. Cross-references

- **TICKET-002** — `docs/FOLLOWUP_TICKETS.md`, §TICKET-002 (root policy + decision center).
- **TICKET-006** — `docs/FOLLOWUP_TICKETS.md`, §TICKET-006 (precedent for target_link_libraries PRIVATE gen-exp guard: `tests/renderer_tests.cmake` used `$<$<TARGET_EXISTS:chronon3d_backend_text>:chronon3d_backend_text>`). PR-A's fix mirrors this pattern.
- **TICKET-008** — `docs/FOLLOWUP_TICKETS.md`, §TICKET-008 (recently closed 2026-06-21; PR-B and PR-C should land on top of its commit chain to avoid further rebase pain).
- The new `TextSpec` declaration: `include/chronon3d/scene/builders/builder_params.hpp:121`.
- `FontSpec`: `include/chronon3d/text/font_engine.hpp:57`.
- `TextContent`, `TextLayoutSpec`, `TextAppearanceSpec`: all in `include/chronon3d/scene/builders/builder_params.hpp`.
- `centered_text` helper: `content/text/text_helpers.hpp` (and `.cpp`).
- Cinematic call sites: `content/anims/compositions/cinematic_text_camera.cpp` lines 142, 256, 350, 450, 486, 599.

---

## 8. One-paragraph summary (for commit message)

`TextSpec`'s composable redesign (TextContent + FontSpec + TextLayoutSpec + TextAppearanceSpec + position) replaced the legacy 30-field `TextParams` monolith. Pre-existing call sites in `content/` that still use the legacy shape (`.text = "X"`, `.font_size = N`, `.font_spec = X`, top-level `.box`/`.align`/`.tracking`/`.color`) need a mechanical transform: top-level fields fan out into the four sub-structs. The rot is concentrated in `chronon3d::content::text::centered_text(...)` (one helper rewrite, 6 inline callsites re-route through it) plus a parallel 1-line CMake glue fix that lets `cmake --preset linux-full-validation` actually exercise the content/ tree (otherwise configure dies earlier on `experimental/expressions/tests/CMakeLists.txt:27` and the rot can only be verified via static grep). After PR-A + PR-B land, the historical "102+" figure is replaced by machine-verified rc=0 on the full set of presets, and TICKET-002 moves to 🟢 Done.
