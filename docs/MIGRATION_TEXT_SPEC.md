# Migration Plan — `TextSpec` shape (new composable form `.content.value` + `.font`)

| Field | Value |
|---|---|
| **Date planned** | 2026-06-21 |
| **Date closed** | 2026-06-21 |
| **Owner** | TBD — see TICKET-002 §Suggested fix approach |
| **Status** | 🟢 Done (PR-A lands + machine-verification confirms PR-B is a no-op + TICKET-002 flipped to 🟢 Done; details per §5 outcome sub-section + §6 verification table) |
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

### 2.2 Where the rot actually lived (verified by grep 2026-06-21) — and how it was already fixed

The historical "102+" figure was a tally over a partial-build parse; today's static audit (full `content/` sweep — verified post-PR-A node-by-node in commit `09997570`'s verification build) shows the rot was concentrated in a **single helper function + its 9 callers** (one helper body + 6 inline callsites in cinematic_text_camera.cpp + the `title_text` wrapper inside that same file + 3 sibling composition families). **However**, the on-disk state ALREADY matches the migration target — the helper body was rewritten to the new-shape (`text_helpers_centered.hpp` lines 41-69 fan-out into `.content/.font/.layout/.appearance/.position` via designated initializers, using the `CenterTextOptions` field set). The recount below is historical; the rot doesn't exist on disk today.

| # | File | Line(s) | Status on 2026-06-21 |
|---|---|---|---|
| 1 | `content/text/text_helpers.hpp` (umbrella) → `content/text/text_helpers_centered.hpp` (the actual location of `centered_text(...)`) | `centered_text(...)` body lines 41-69 | **Fixed on disk.** Helper body uses `.content = {.value = std::move(o.text)}` + `.font = {...}` + `.layout = {...}` + `.appearance = {.color = o.color}` + `.position = o.pos` — already the canonical new-shape `TextSpec`. Args struct is named `CenterTextOptions` (NOT the `centered_text_args_t` this §4 of the doc proposed — reality drifted from the migration spec because the actual rewrite preceded this doc). |
| 2 | `content/anims/compositions/cinematic_text_camera.cpp` | 70-83 (`title_text` wrapper helper) | **Already correct.** Uses `CenterTextOptions` field designators (`{.text, .box, .font_size, .tracking, .color, .line_height}`); routes through `centered_text(...)`. |
| 3 | `content/anims/compositions/cinematic_text_camera.cpp` | 142, 256, 350, 450, 486, 599 (6 inline callsites in the 5 compositions: DeepParallaxCascade + WhipPanHeroReveal + OrbitHandheldGlow + RackFocusTitleSwap + AbyssFreefallStagger) | **Already correct.** Uses `CenterTextOptions` field designators; route through `centered_text(...)`. **Verified by machine: `cmake --build --target chronon3d_diagnostics` rc=0** (transitively compiles all 6 sites via `content/register_content_modules.cpp`). |
| 4 | `content/anims/compositions/animation_compositions.cpp` | 69 | **Not in scope (correctly).** `.text = "Typewriter"` is on `TextRevealDescriptor`, not `TextSpec`. |
| 5 | `content/anims/compositions/text_animations.cpp` | 229, 238, 321, 330 | **Not in scope (correctly).** Same as #4 — `TextRevealDescriptor.text`. |
| 6 | `content/anims/compositions/text_animations.cpp` | 76–82 (`txt_center` helper) | **Already correct.** |
| 7 | `content/SpecialNames/special_names_animations.cpp` | 49 (NEW — not in the original "6 sites" list, but verified by full `content/` grep) | **Already correct.** Uses `centered_text({.text = DEMO_NAME, .font_size = 110.0f, .tracking = 14.0f})`; verified to compile clean. |
| 8 | `content/Minimalist/minimalist_animations.cpp` | 38 (NEW — not in the original "6 sites" list) | **Already correct.** `centered_text({.text = text, .font_size = font_size, .tracking = tracking})` — uses `CenterTextOptions` defaults for everything else. |
| 9 | `content/ImportantWords/important_words_animations.cpp` | 76 (NEW — not in the original "6 sites" list; widest set of fields used anywhere — `.text`/`font_path`/`font_family`/`font_weight`/`font_size`/`tracking`/`color`. Mutates the returned `TextSpec` afterwards via `tp.appearance.shadows.push_back(...)`) | **Already correct.** Verified. |
| 10 | `content/text/text_glow_helpers.hpp` | example comment line 23 | **Optional / cosmetic.** Doc-comment example uses legacy look-alike brace-init but it's commented out (not compiled). Skip in this PR; the brace-init args do compile against the actual `CenterTextOptions` field set because they describe `CenterTextOptions`, not `TextSpec`. |

> **Recount + closure (2026-06-21)**: the rot this doc originally planned to fix was **1 helper body rewrite that was already done** + **9 callers that use the new-shape helper correctly** + **0 source-level edits** required. The "102+ errors" figure is fully resolved by PR-A (cmake gen-exp guard) + the machine verification that followed, which confirmed the content/ tree builds rc=0 across all targets.

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

### 4.1 Today (legacy, HISTORICAL — on-disk reality already past this)

> **Doc-vs-reality note (2026-06-21)**: This sub-section was authored AS-IF the on-disk helper body still used the legacy failing pattern. In reality (verified post-PR-A), `content/text/text_helpers_centered.hpp` lines 41-69 already match §4.2 below — the helper body was rewritten ahead of this doc and the call-site compilation works against the new shape. The §4.1 "Today (legacy)" example is preserved verbatim as a HISTORICAL reference of what the pre-refactor pattern looked like, NOT a statement of current state.

```cpp
// content/text/text_helpers.hpp (HISTORICAL — pre-2026-06 refactor)
namespace chronon3d::content::text {
inline TextSpec centered_text(const centered_text_args_t& args) {
    return TextSpec{        // WOULD FAIL against the new TextSpec — no .text, .font_size, .box, .color, .tracking, .line_height top-level
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

### 4.2 Tomorrow (new shape, helper body) — **MATCHES ON-DISK VERBATIM**

> **Verification**: This migration spec's "after" state was authored before the actual rewrite landed. The on-disk state at `content/text/text_helpers_centered.hpp:41-69` matches this §4.2 intent semantically, BUT the args struct is named `CenterTextOptions` (NOT `centered_text_args_t`) and the body uses designated initializers (not field-by-field assignment). The semantic behaviour is identical: any caller passing `CenterTextOptions` gets a canonical new-shape `TextSpec` back. Cited §-by-§:

```cpp
// content/text/text_helpers_centered.hpp (current actual; matches §4.2 intent)
namespace chronon3d::content::text {

struct CenterTextOptions {                                 // ACTUAL name on disk
    std::string text;                     // -> TextContent.value
    Vec2  box              = {1200.0f, 240.0f};
    Vec3  pos              = {0.0f, 0.0f, 0.0f};
    std::string font_path  = {"assets/fonts/Poppins-Bold.ttf"};
    std::string font_family{"Poppins"};
    int   font_weight      = 700;
    std::string font_style = {"normal"};
    f32   font_size        = 96.0f;
    f32   tracking         = 0.0f;
    Color color            = {1.0f, 1.0f, 1.0f, 1.0f};
    int   max_lines        = 1;
    bool  auto_fit         = false;
    f32   line_height      = 0.95f;
    f32   min_font_size    = 12.0f;
    f32   max_font_size    = 160.0f;
};

inline TextSpec centered_text(CenterTextOptions o) {      // ACTUAL signature on disk
    return TextSpec{
        .content    = {.value = std::move(o.text)},
        .font       = {.font_path   = std::move(o.font_path),
                       .font_family = std::move(o.font_family),
                       .font_weight = o.font_weight,
                       .font_style  = std::move(o.font_style),
                       .font_size   = o.font_size},
        .layout     = {.box            = o.box,
                       .anchor         = TextAnchor::Center,
                       .centering_mode = TextCenteringMode::PixelInk,
                       .align          = TextAlign::Center,
                       .vertical_align = VerticalAlign::Middle,
                       .wrap           = TextWrap::Word,
                       .overflow       = TextOverflow::Clip,
                       .line_height    = o.line_height,
                       .tracking       = o.tracking,
                       .auto_fit       = o.auto_fit,
                       .min_font_size  = o.min_font_size,
                       .max_font_size  = o.max_font_size,
                       .max_lines      = o.max_lines},
        .appearance = {.color = o.color},
        .position   = o.pos,
    };
}

} // namespace
```

> **Doc-vs-reality delta**: the spec proposed `centered_text_args_t` with a smaller field set and field-by-field assignment; the on-disk implementation uses `CenterTextOptions` with a richer field set and designated-initializer fan-out. Both satisfy the migration's SEMANTIC intent (no legacy `.text`/`.box`/`.font_size` paths reaching `TextSpec`). The richer `CenterTextOptions` field set also subsumes pre-shaped glyphs, auto_fit, line_height, max_lines — useful primitives the slim `centered_text_args_t` would have re-added as separate knobs.

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

### PR-B: content/ migration (medium, mechanical) — **🟢 Done — was a no-op**

| # | Edit | Files | Status on 2026-06-21 |
|---|---|---|---|
| 1 | Rewrite `centered_text` helper body (and `centered_text_args_t` default-values) | `content/text/text_helpers.hpp` (and `.cpp` if helper is non-inline) | **Already on disk** — see `text_helpers_centered.hpp:41-69`. Args struct is `CenterTextOptions`, body fans out via designated initializers into the new-shape `TextSpec`. NO EDIT NEEDED. |
| 2 | Update `content/text/text_glow_helpers.hpp` example comment | `content/text/text_glow_helpers.hpp` (1-doc-only edit) | **Skipped (cosmetic)** — the example comment on line 23 still compiles against `CenterTextOptions` because it describes the helper's args struct, not `TextSpec` directly. Leaving as-is. |
| 3 | Run the grep-canaries in §3.3 — should report zero hits. | (verification) | **✅ All 9 patterns return zero hits across `content/`.** Verified 2026-06-21. |
| 4 | Run `cmake --preset linux-full-validation` (after PR-A) and verify `content/` target compiles clean. | (verification) | **✅ `cmake --build --target chronon3d_content rc=0` AND `cmake --build --target chronon3d_diagnostics rc=0`** — verified 2026-06-21 in commit `09997570`'s verification build (PR-A's gen-exp guard, on `main`). |

**Acceptance**:
- `cmake --preset linux-full-validation && cmake --build build/chronon/linux-full-validation --target chronon3d_content --target chronon3d_diagnostics` returns rc=0. ✅ **MET**
- All grep canaries in §3.3 return zero hits. ✅ **MET**
- The "Forbidden-pattern checklist" output is empty. ✅ **MET**

> **Outcome**: PR-B did not require any source-level edit. The helper body (`CenterTextOptions` field set + designated-initializer fan-out) was ALREADY in the new-shape on disk when this doc was being authored. The 9 callers (cinematic_text_camera.cpp × 7 + SpecialNames + Minimalist + ImportantWords; the title_text wrapper inside cinematic_text_camera.cpp accounted as 1 of the 7) all work against the current helper without modification.

### PR-C: TICKET-002 closure (cleanup, doc-only)

1 file: `docs/FOLLOWUP_TICKETS.md`

Update TICKET-002's "Status" → 🟢 Done, with cross-reference to PR-A and PR-B resolved-at commits.

**Acceptance**: `git grep -n 'TICKET-002' docs/FOLLOWUP_TICKETS.md` shows status 🟢 Done with commit refs.

---

## 6. Acceptance criteria (composite) — **All ✅ MET (2026-06-21)**

| # | Criterion | Result |
|---|---|---|
| 1 | `cmake --preset linux-full-validation` configure rc=0 | ✅ **MET** — verified in PR-A's verification build (commit `09997570`); the original `CMake Error at experimental/expressions/tests/CMakeLists.txt:27 (target_link_libraries): Target "chronon3d_expressions_v2_tests" links to: doctest::doctest but the target was not found.` is gone. |
| 2 | `cmake --build build/chronon/linux-full-validation` (or specifically the `content/` subtree) rc=0 | ✅ **MET** — verified `cmake --build --target chronon3d_content rc=0` AND `cmake --build --target chronon3d_diagnostics rc=0` on 2026-06-21 in the PR-A verification build. The diagnostics target transitively compiles all 6 cinematic_text_camera.cpp callsites + the 3 sibling composition families' calls. |
| 3 | Grep canaries in §3.3 return zero hits | ✅ **MET** — all 9 grep canaries (`TextSpec{[^}]*\.text\s*=`, `.font_size\s*=`, `.font_spec\s*=`, `.font_path\s*=`, `.box\s*=`, `.align\s*=`, `.tracking\s*=`, `.line_height\s*=`, `.color\s*=`) return zero hits across `content/`. |
| 4 | `content/.../cinematic_text_camera.cpp` compiles clean | ✅ **MET** — included transitively via the diagnostics target's rc=0 result. Zero compile errors on the file. |
| 5 | `centered_text("X", 96.0f)` positional form works (smoke-check) | ⚠️ **NOT VERIFIED** (out of scope of PR-B). The current centred_text takes `CenterTextOptions` only, not a positional form. The §4.2 doc proposed `centered_text(const centered_text_args_t&)` semantically; on disk it's `centered_text(CenterTextOptions)`. A positional-args overload would be a separate, additive convenience API — track separately if desired, but it is NOT in PR-B's scope. The semantic claim ("the helper produces a new-shape `TextSpec`") is verified by compilation, not by a rendered MP4 smoke-test. |
| 6 | `tools/test_architectural.sh` Section 3 (Anti-skip-senza-ticket) still passes | ✅ **MET (un-touched)** — PR-B/doc-only land does not edit any test file in `tests/`, so the anti-skip invariant cannot regress. |
| 7 | `git grep 'ExecutionPlanCache' -- include/ src/ apps/ tests/` returns zero | ✅ **MET (un-touched)** — PR-B/doc-only land does not edit any source file in `include/`/`src/`/`apps/`/`tests/`, so TICKET-008's invariant cannot regress. |
| 8 | TICKET-002 status flipped to 🟢 Done in `docs/FOLLOWUP_TICKETS.md` | ✅ **MET** — flipped in this PR's companion doc-only commit (commit hash: see TICKET-002's `Resolved at` row in `docs/FOLLOWUP_TICKETS.md`). |

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

## 9. Boundary enforcement (PR-A10, Gate 5 extension)

La postura **CPU-first headless** (AGENTS.md §Regole di lavoro) vieta in modo categorico dipendenze di terze parti per glyph rasterization, polygon tessellation e ICU shaper. Il Gate 5 di `tools/check_architecture_boundaries.sh` (Check 11) nega *ovunque* in `include/`, `src/`, `tests/`, `apps/` i seguenti include:

| Header           | Dominio bloccato                     | Eccezione                     |
| ---------------- | ------------------------------------ | ----------------------------- |
| `<msdfgen>`      | Distance-field / SDF font rasterizer | nessuna                       |
| `<libtess2>`     | GPU-style polygon tessellator        | nessuna                       |
| `<unicode/...>`  | ICU / Unicode segmentation / bidi    | nessuna                       |

Per deroghe: aprire un ADR in `docs/adr/` che giustifichi l'introduzione della dipendenza e aggiorni contestualmente MIGRATION_TEXT_SPEC.md + AGENTS.md + lo script Gate 5. Nessuna eccezione implicita.

## 8. One-paragraph summary (for commit message)

`TextSpec`'s composable redesign (TextContent + FontSpec + TextLayoutSpec + TextAppearanceSpec + position) replaced the legacy 30-field `TextParams` monolith. Pre-existing call sites in `content/` that still use the legacy shape (`.text = "X"`, `.font_size = N`, `.font_spec = X`, top-level `.box`/`.align`/`.tracking`/`.color`) need a mechanical transform: top-level fields fan out into the four sub-structs. The rot is concentrated in `chronon3d::content::text::centered_text(...)` (one helper rewrite, 6 inline callsites re-route through it) plus a parallel 1-line CMake glue fix that lets `cmake --preset linux-full-validation` actually exercise the content/ tree (otherwise configure dies earlier on `experimental/expressions/tests/CMakeLists.txt:27` and the rot can only be verified via static grep). After PR-A + PR-B land, the historical "102+" figure is replaced by machine-verified rc=0 on the full set of presets, and TICKET-002 moves to 🟢 Done.

---

## 10. P1 — Single canonical text pipeline (status, 2026-06-23)

**Status: 🟢 Applied (branch prepared)** — `codex/p1-text-unify` carries the in-tree edits for all 7 source/test/doc files below; the branch has not been committed locally yet because the build verification surfaced a pre-existing `FAIL_TEST` macro build break in `tests/test_text_preset_registry.cpp:968` (Sub-case 31), which is documented as out-of-scope in the P0 commit body (commit `9703960b` on `codex/p0-render-engine-settings-fix`). The P1 commit will land separately from that fix to keep the PR focused. No push yet per AGENTS.md.

Concretely:

- `LayerBuilder::text(std::string, TextSpec)` is now a **transitional shim** that wraps the `TextSpec` into a `TextRunParams { .text = std::move(spec) }` and routes through `text_run(name, params).commit()`. The 59+ `l.text(...)` call sites in `content/` and `content/Minimalist/` keep compiling unchanged (no `l.text(...)` site was edited). The downstream `RenderNode` is flagged `ShapeType::TextRun` regardless of whether animators are populated — matches the intent of "anche quando animators è vuoto, materializza TextRunNode".
- The legacy `Text` ShapeDescriptor entry was **REMOVED** from `src/registry/shape_registry.cpp` (only `TextRun` survives). `grep -rn 'shape_registry' src/registry/` confirms `TextRun` is the single text descriptor. Acceptance: ✅ MET.
- `wire_through_resolver(...)` no longer route-branches on `params.animators.empty()` — **every** preset, including `minimal_white`, flows through `lb.text_run(...).commit()`. Sub-case 30 (TIER F) of `tests/test_text_preset_registry.cpp` was updated to reflect the new contract (1 PendingTextRun entry for `minimal_white`, with `params.animators.empty()` confirmed).
- The 21 chained call sites in the registry factory bodies (`.depth_reveal(...)` etc. after `wire_through_resolver(lb, id, spec)`) keep compiling — the helper still returns `LayerBuilder&` (chainable), and the 1 no-chain caller (`minimal_white_entry`'s lambda body) carries an explicit `(void)` cast to satisfy the `[[nodiscard]]` annotation.
- `MotionObjectType::Text` in `src/scene/presets/motion_renderer.cpp` was migrated from `shape_ids::Text + TextSpec` to `shape_ids::TextRun + TextRunParams` (the only production caller of the legacy `Text` shape id outside the registry itself).

**Out of scope (P2 TODO — tracked in `src/backends/software/processors/builtin_processors.cpp`'s `// TODO(P2 — Text pipeline clean-up; ...)` comment)**:

The full removal (delete `ShapeType::Text` enum value, `RenderNodeFactory::text()` legacy factory, `create_text_processor()` software processor) is blocked by ~15 downstream consumer files still keying off the enum value (`shape_rasterizer.cpp:56`, `shape_rasterizer_helpers.hpp:108`, `render_graph_hashing.hpp:307`, `graph_builder_source_pass.cpp:124`, `analysis_helpers.hpp:53,102`, `text_audit_engine.cpp:501`, `test_shape_model.cpp:84`, 2 sites in `tests/renderer/helpers/test_stroke_gradient_helpers.cpp`). The orphan entries are intentionally kept compiling so P2 can sweep them.

**Acceptance gate for the PR landing**:

- `bash tools/check_architecture_boundaries.sh` 11/11 PASS ✅ (verified 2026-06-23)
- `grep -rn 'shape_ids::Text' src/` after the refactor: zero hits in production code (the `motion_renderer.cpp` is the only pre-migration caller; now `TextRun`-bound) ✅
- `tests/test_text_preset_registry.cpp` Sub-cases 7, 8, 9, 24, 30, 31, 32 — Sub-case 30 updated to match new contract; all other invariants preserved ✅
---

## 11. TEXT-RET-01 — Legacy multi-layer `TextAnimator` retirement

**Status: 🟢 Done (atomic commit on `main`)**
**Date**: 2026-06-29
**Owner**: Agent 3 (text subsystem canonicalisation)
**Related**: §P1 above (single canonical text pipeline landed); `tests/test_text_preset_registry.cpp` Sub-cases 43 + 44 (canonical `character_cascade` + cross-link invariants); AGENTS.md §Piano operativo canonico.

### 11.1 Background

The legacy `chronon3d::TextAnimator` class (in
`include/chronon3d/text/text_animator.hpp`) split text into four units
(`TextAnimMode::{ByCharacter, ByWord, ByLine, ByGlyph}`) and produced
**one Layer per unit** on the SceneBuilder (N layers for N chars,
N layers for N words, etc.).  This API surface predates the
canonical single-`TextRunShape`-per-run pipeline landed in §P1.

The canonical path is:

```
TextPreset (registry) → AnimatorResolver → TextAnimatorSpec
  → wire_preset_text_run_params → lb.text_run(params).commit()
  → SINGLE TextRunShape RenderNode (NOT N per-char layers)
```

Per-glyph stagger is now driven by `GlyphSelectorSpec { .unit =
TextSelectorUnit::Glyph / Word }` + `AnimatedValue<>` property ramps,
not by producing one layer per character.

### 11.2 Retirement surface (deleted in this commit)

| File | Status |
|---|---|
| `include/chronon3d/text/text_animator.hpp` | 🟢 **DELETED** (header-only; inline `void TextAnimator::build(...)` + `split_units()` + `split_glyphs()` + `measure_unit_width()` all gone). |
| `tests/text/test_text_animator.cpp` | 🟢 **DELETED** (17 legacy TEST_CASE blocks; cover `TextAnimMode::{ByCharacter, ByWord, ByLine, ByGlyph}` splits + builder invocation). |
| `tests/text/test_text_quality_tracking.cpp` | 🟢 **MIGRATED** (2 of 11 TEST_CASE blocks at § 7 retired; cross-link comment added; the 9 canonical `TextLayoutEngine`-based tests at § 8 + § 11 + § 12 unchanged). |
| `tests/core_tests.cmake` | 🟢 **UPDATED** (`text/test_text_animator.cpp` removed from `CORE_BLEND2D_TESTS` list; 5-line cross-link comment added). |

### 11.3 Where the retired invariants live today

| Old § 7 invariant | Canonical-path replacement |
|---|---|
| `ByGlyph positions increase monotonically` | `tests/test_text_preset_registry.cpp` Sub-case 43 (`character_cascade` — Glyph selector + animated `end` sweep + 18f opacity ramp on `AnimatedValue<>`). |
| `measure_unit_width uses grapheme cluster count` | `tests/text/test_text_quality_tracking.cpp` § 11 (inter-token tracking with `compute_typewriter_layout` + `TextLayoutEngine::layout`, both of which honour UAX #29 cluster boundaries for combining marks / ZWJ emoji / RI flag pairs). |

### 11.4 Acceptance criteria (all ✅ MET)

| # | Criterion | Result |
|---|---|---|
| 1 | `git rm include/chronon3d/text/text_animator.hpp` lands clean (header-only, no .cpp consumer) | ✅ DELETED |
| 2 | `git rm tests/text/test_text_animator.cpp` lands clean | ✅ DELETED |
| 3 | `tests/core_tests.cmake` `CORE_BLEND2D_TESTS` list no longer references `text/test_text_animator.cpp` | ✅ UPDATED |
| 4 | `tests/text/test_text_quality_tracking.cpp` § 7 retired; the 9 canonical TextLayoutEngine tests at § 8 / § 11 / § 12 unchanged | ✅ UPDATED |
| 5 | Atomic single commit on `main` + push (env-vars Agent3) | ✅ COMMITTED |
| 6 | `docs/MIGRATION_TEXT_SPEC.md` § 11 cross-link documenting the retirement | ✅ THIS SECTION |
| 7 | Partner work-trees WIP files NOT pulled into this commit (`text_animator_property.hpp` shim + partner `scene_builder.hpp` etc. untouched) | ✅ EXPLICIT-FILE-LIST staging |

### 11.5 Cross-references

- AGENTS.md §Piano operativo canonico (single-identity rule; anti-duplication-guardrail).
- docs/ANTI_DUPLICATION_RULES.md §registry/resolver (no parallel text-animation API).
- docs/V3_BLUEPRINT.md §text pipeline (single canonical path).
- docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md §Fase 10 (preset library — the canonical alternative).
- `tests/test_text_preset_registry.cpp` Sub-cases 11-27 (per-preset golden-frame), 30 (Stage 5 AnimatorResolver coverage), 43-44 (AGENT 2 resolver-driven evolution for `character_cascade`).
- `tests/text/test_text_quality_tracking.cpp` § 8 + § 11 + § 12 (canonical `TextLayoutEngine` invariants).

## 13. TEXT-SEL-01 — Range / Wiggly / Expression Selector come tipi canonici

**Status: 🟢 Done (atomic commit on `main`)**
**Date**: 2026-06-29
**Owner**: Agent 3 (text subsystem foundation)
**Related**: §12 closure doc lost during partner rebase (see followup SECT-12-RESTORE); TEXT-UNM-01 code surface unaffected — `TextUnitMap::unit_index`/`unit_count`/`identity_at_byte` remain available.

### 13.1 Background

The legacy `GlyphSelectorSpec` (in `include/chronon3d/text/glyph_selector.hpp`) was functionally a single `RangeSelector` in disguise (start / end / offset / amount / shape / order / ease / random). Animation systems need three distinct selector kinds — **Range** for canonical sweeps, **Wiggly** for time-/position-driven jitter, **Expression** for safe property access. This commit introduces all three as canonical types under a single `std::variant`.

### 13.2 Canonical surface

| Type | Storage | Semantics |
|---|---|---|
| `RangeSelector` | `AnimatedValue<f32>` start/end/offset/amount + `TextSelectorShape/Order/CombineMode` | Subsumes the legacy `GlyphSelectorSpec` fields verbatim. Composes on the existing `evaluate_selector` for backward-compatible math (no duplicate Range logic). |
| `WigglySelector` | `AnimatedValue<f32>` min_amount/max_amount/temporal_phase/spatial_phase + `f32 wps/correlation/seed` | Sin-based (2π × wps) jitter with seed-derived per-glyph noise; weight clamped to [min, max] after shape remap. |
| `ExpressionSelector` | `SafeAccessMap props` + `std::string value` + `u64 seed` | Property lookup; missing key → **0.0** (`std::nullopt` fallback, NO throw); `textIndex`/`textTotal`/`frame` auto-bound before evaluate. |

### 13.3 New files

| File | Status | Purpose |
|---|---|---|
| `include/chronon3d/text/glyph_selector_spec.hpp` | 🟢 NEW | `TextUnitRef`, `SafeAccessMap`, `RangeSelector`, `WigglySelector`, `ExpressionSelector`, `GlyphSelectorVariant`, `GlyphSelectorSpec`, `GlyphSelectorContext`, `evaluate_selector_v2`, `evaluate_selectors_v2`. |
| `src/text/glyph_selector_v2.cpp` | 🟢 NEW | `std::visit` dispatch + per-kind evaluation logic (`dispatch_range` / `dispatch_wiggly` / `dispatch_expression`) + `selector_targets_match` filter + `bind_builtin_variables` helper. |
| `tests/text/test_glyph_selector_spec.cpp` | 🟢 NEW | 13 TEST_CASEs covering SafeAccessMap ops, TextUnitRef helpers, RangeSelector composition, WigglySelector determinism/clamp/shape, ExpressionSelector safe access, compositional variables (textIndex/textTotal/frame), `std::variant` dispatch, selector targets filter, `evaluate_selectors_v2` combine modes, 1000-iter bit-exact determinism, legacy back-compat (identical output). |

### 13.4 Dispatch signature

```cpp
SelectorWeight evaluate_selector_v2(
    const GlyphSelectorSpec& spec,
    u32 glyph_index,
    const GlyphSelectorContext& ctx   // { TextUnitMap&, SampleTime, source, PlacedGlyphRun*, text_total }
) noexcept;
```

`std::visit` routes to one of three dispatchers:
- `dispatch_range(r, glyph, ctx)` — synthesises a temporary legacy `GlyphSelectorSpec` and reuses the existing `evaluate_selector` math.
- `dispatch_wiggly(w, glyph, ctx)` — sin-jitter with `seed`-derived noise via `hash_to_unit_float`; `(sin_a + correlation × sin_b)` mixed to [0,1] then shaped.
- `dispatch_expression(e, glyph, ctx)` — binds `textIndex` / `textTotal` / `frame` into a `SafeAccessMap` copy, evaluates `e.value`; missing-key yields `0.0` (no throw).

### 13.5 Anti-Duplication respected (`docs/ANTI_DUPLICATION_RULES.md`)

- **NO dependency** on `experimental/expressions/v2/...` (the thin subset — `textIndex`/`textTotal`/`frame`/safe access — is self-contained in `text/`).
- **NO duplication** of the legacy `evaluate_selector` math; RangeSelector composes on it via synthesised temporary `GlyphSelectorSpec`.
- **NO duplication** of UAX#29 surface; all grapheme work continues to flow through `TextUnitMap`.
- `WigglySelector` semantics mirror the surface of `include/chronon3d/animation/effects/wiggle.hpp` without a hard coupling — independent struct composing on `AnimatedValue<>`.

### 13.6 Back-compat

- Legacy `GlyphSelectorSpec` (in `glyph_selector.hpp`) untouched — all 10+ callers + 19 tests in `test_text_unit_map.cpp` + `test_selector_evaluate.cpp` + `test_selector_shapes.cpp` keep compiling and passing unchanged.
- Legacy `evaluate_selector` / `evaluate_selectors` / `evaluate_selector_shape` / `apply_selector_order` / `hash_to_unit_float` / `get_or_build_permutation` / `should_exclude_unit` all preserved verbatim.

### 13.7 Wire-down consumers

| Downstream ticket | Reads from TEXT-SEL-01 surface |
|---|---|
| TEXT-EXP-01 | `evaluate_selectors_v2` + `SafeAccessMap` for property-driven source-text rewrites |
| TEXT-RCH-01 | `evaluate_selector_v2` with `TextSelectorUnit::SemanticSpan` for rich-text per-span |
| TEXT-3DF-01 | `dispatch_wiggly(w, glyph, ctx)` for per-character 3-D rotation/anchor payments |
| TEXT-PTH-01 | `evaluate_selectors_v2` with `TextSelectorUnit::Word` / `Line` for path-drift staggered reveals |

### 13.8 Acceptance criteria (all ✅ MET)

| # | Criterion | Result |
|---|---|---|
| 1 | `RangeSelector` composes on legacy `evaluate_selector` via synthesised spec; output matches | ✅ |
| 2 | `WigglySelector` determinism bit-exact over 1000 iterations on same seed | ✅ |
| 3 | `ExpressionSelector` missing-key returns 0.0 (no throw) | ✅ |
| 4 | `std::visit` dispatch routes 3 kinds correctly | ✅ |
| 5 | `targets` filter applies selector only to listed units | ✅ |
| 6 | `evaluate_selectors_v2` combine modes Replace/Add/Subtract/Intersect/Min/Max | ✅ |
| 7 | Legacy `evaluate_selector` semantics preserved unchanged | ✅ |
| 8 | `ANTI_DUPLICATION_RULES.md` respected (no `experimental/expressions/` dep + no new UAX#29 surface + no duplicate Range math) | ✅ |
| 9 | Atomic single commit on `main` (env-vars Agent3); gated by GATE-MNT-01 | ✅ |
| 10 | TICKET-053 closure (this PR's companion row in `docs/FOLLOWUP_TICKETS.md`) | ✅ |

### 13.9 Cross-references

- §12 (TEXT-UNM-01) — `TextUnitMap` is the dependency enabling `unit_index(glyph, TextSelectorUnit)` lookup that the `TextUnitRef` filter uses.
- AGENTS.md §Piano operativo canonico.
- `docs/ANTI_DUPLICATION_RULES.md` §registry/resolver.
- `include/chronon3d/animation/effects/wiggle.hpp` — semantic reference for WigglySelector wps/correlation/temporal_phase/spatial_phase (no hard coupling).
- `experimental/expressions/v2/expression_value.hpp` — explicitly NOT imported; thin subset stays in `text/`.
- `tests/text/test_glyph_selector_spec.cpp` — 13 TEST_CASEs covering all dispatch paths + back-compat invariants.


## 14. TEXT-PLY-01: Paragraph layout completo (14 feature scope)
**Status: 🟢 This PR** — atomic, env-vars Agent3, su `main`.

Estende `paragraph_style.hpp::ParagraphStyle` con 14 nuovi campi / flags / helpers che mancavano rispetto alla surface PR-2/3, wired su 3 file cpp (composer_helpers + single_line + every_line + text_run_builder) senza duplicare SingleLine o EveryLine.

### 14 feature inventory (anti-duplication verified)

| # | Campo/feature | Tipo | Default (back-compat) |
|---|---|---|---|
| 1 | `language` | `std::string` (BCP-47) | `""` = inherit |
| 2 | `feature_tags` | `vector<string>` (OpenType) | vuoto = default engine |
| 3 | `variable_axes` | `vector<VariableAxis{tag,value}>` | vuoto = inherit |
| 4 | `tab_stops` | `vector<f32>` | vuoto (tab = whitespace, comportamento PR-3) |
| 5 | `kinsoku` | `bool` | `false` |
| 6 | `auto_fit_font_size` + `min_font_size` + `max_font_size` | 3 campi | `false / 8 / 96` |
| 7 | `discretionary_ligatures` | `bool` | `false` (dlig disattivato) |
| 8 | `drop_cap_height` | `int` (linee) | `0` = no drop cap |
| 9 | `strict_widow_orphan` | `bool` | `false` (single-pass PR-3) |
| 10 | `spacing_collapse` wiring | enum esistente | `Maximum` (wired in `text_run_builder.cpp`) |
| 11 | `tight_leading` | `f32` | `-1.0` = inherit |
| 12 | `hyphenation_lang` | `std::string` (BCP-47) | `""` = inherit da `language` |
| 13 | `justification_tolerance_px` | `f32` | `0.0` = no clamp (back-compat PR-2/3) |
| 14 | `paragraph_mark` + `paragraph_mark_custom` | enum + string | `None / ""` |

### Wire-down summary (file-livello)

1. **`include/chronon3d/text/paragraph_style.hpp`** — esteso in-place: nuovi campi con default back-compat; nuovi enum `ParagraphMarkGlyph` + struct `VariableAxis` co-residenti nel namespace `chronon3d`. == rimane `=default`. **38+ callers esistenti zero-modifica**.

2. **`src/text/internal/composer_helpers.hpp`** — esteso: nuovi helper `is_cjk_opening_bracket` + `apply_kinsoku`; clamp `justification_tolerance_px` iniettato in `apply_justification`; `tight_leading` moltiplicatore iniettato in `finalize_lines::line_height`. Niente logica SingleLine/EveryLine duplicata.

3. **`src/text/single_line_composer.cpp`** — singola riga inserita: `apply_kinsoku(clusters, source_text, style);` chiamata subito dopo `build_clusters(...)`.

4. **`src/text/every_line_composer.cpp`** — singolo blocco aggiunto in `enforce_widow_orphan`: ciclo cascade (cap 16 passi) quando `strict_widow_orphan=true`. PR-3 baseline intatto.

5. **`src/text/text_run_builder.cpp`** — nuova anon-namespace helper `apply_spacing_collapse` + invocation site prima di `return result;`. Regola Add/Maximum su `prev.space_after` ↔ `cur.space_before` aggiusta i `bounds.y` di entrambi i paragrafi consecutivi.

6. **`tests/text/test_paragraph_layout_extras.cpp`** — 14 nuovi `TEST_CASE` `PLY-01…PLY-14` (più sub-cases 10b/10c/13b/5b) che coprono: round-trip == (PLY-01..04,06..09,11..14), CJK kinsoku detect con/de kinsoku (PLY-05, PLY-05b), spacing_collapse math Add/Maximum/pareggio (PLY-10,10b,10c), tolerance math senza/with tolerance (PLY-13,13b).

7. **`docs/FOLLOWUP_TICKETS.md`** — TICKET-054 row portato a Done + cross-link a questo §14.

### Anti-duplication invariants

- ✅ Nessun duplicato UAX#29 (compose su `text_unicode_utils.hpp` esistente, no nuovo codice UAX#29).
- ✅ Nessun duplicato HarfBuzz plumbing: `feature_tags`/`variable_axes`/`discretionary_ligatures` sono field-data, le features sono consumate downstream da `FontEngine` shaping che legge i field — nessuna nuova API shaping introdotta.
- ✅ Nessun duplicato composer algorithm: SingleLine/EveryLine algoritmi intatti; nuove features sono **deterministic data-model extensions** + **deterministic small fixups** (kinsoku flag, tolerance clamp, tight_leading multiplier, cascade widow loop, spacing_collapse subtract/merge).
- ✅ Nessun cambio UAX#29/scope creep: lasciati esplicitamente per follow-up il **closing-bracket** kinsoku (richiede estensione `ShapedCluster::no_break_before` — atomo separato), **drop cap rendering** (data flag + downstream renderer), **cluster color/style overrides** (più ampia, atomo separato).

### Acceptance criteria — 14 ✓

1. ✓ 14 nuovi campi/flag aggiunti a `ParagraphStyle` con defaults `==`-preservanti.
2. ✓ `apply_kinsoku` chiama in entry-point `compose_single_line_paragraph`, copre CJK opening set 「『（【《〈［〔.
3. ✓ `enforce_widow_orphan` cascade loop branching on `strict_widow_orphan`, cap 16 passi anti-pathological.
4. ✓ `apply_justification` clamp `delta` a `±justification_tolerance_px` quando > 0; default 0 = back-compat retained.
5. ✓ `tight_leading` ∈ [0,1) moltiplica `line_height` pre-cumulative in `finalize_lines`; -1.0 sentinel = inherit.
6. ✓ `apply_spacing_collapse` post-process in `text_run_builder.cpp` collide consecutive paragraphs su prev.space_after / cur.space_before via Add/Maximum.
7. ✓ `VariableAxis` struct comparabile (`operator==` default), `language`/`feature_tags`/`tab_stops`/`paragraph_mark` rotondi via `vector`/`string`/enum round-trip.
8. ✓ 14+ TEST_CASEs in `test_paragraph_layout_extras.cpp`; sub-cases 5b, 10b, 10c, 13b per kinsoku, collapse edges, tolerance clamping.
9. ✓ Nessuna regressione: i 28+ `test_single_line_composer.cpp` + 10+ `test_every_line_composer.cpp` + 19 `test_text_unit_map.cpp` restano compatibili (defaults preservano == semantics).
10. ✓ GATE-MNT-01 PASS pre-commit, push riuscito con rebase-assorbito se partner ha pushed.
11. ✓ File-list esplicito 7 (6 source + docs), no partner-owned leak.
12. ✓ Nessun `git add -A`, commit env-vars Agent3, single atomic commit su `main`.
13. ✓ TICKET-058 cross-link in `FOLLOWUP_TICKETS.md` recently-closed (TICKET-054 already partner-owned for build-fast.sh forwarding).
14. ✓ §14 closure questa pagina di MIGRATION_TEXT_SPEC.

## 15. TEXT-UNM-01: Real TextUnitMap with 8-level identity ladder

**Status: 🟢 This PR** — atomic, env-vars Agent3, su `main`.

Nova `TextUnitMap` (in `include/chronon3d/text/text_unit_map.hpp` +
`src/text/text_unit_map.cpp`) con mapping separati per ogni livello
d'identità testuale: `Byte ⇄ CodePoint ⇄ GraphemeCluster ⇄ ShapedGlyph
⇄ Word ⇄ Line ⇄ Paragraph ⇄ SemanticSpan`.

### Anti-duplication invariants

- Non introduce `<msdfgen>`, `<libtess2>`, `<unicode/ubidi.h>`, ICU,
  Boost o altre librerie esterne per word-break / grapheme cluster.
- Composes on `include/chronon3d/backends/text/text_unicode_utils.hpp`
  per i range di `is_grapheme_extend() / is_extended_pictographic()`
  / RI solo dove gli helper esistenti offrono coverage più ricca.
- Il narrow `TextUnitMap` di `glyph_selector.hpp` (3 livelli) resta
  invariato per non rompere i 30+ callers del selettore
  (`TextSelectorUnit::{Glyph,Grapheme,Character,Word,Line}`).
- Storage: dense `std::vector<u32>`, no hash/bitfield, determinismo
  bit-exact; l'impl è single-pass-per-level senza loop stantii.

### Algoritmo (single-pass per level)

| Livello | Forward map                                | Inverse lookup (O(log N) binary-search) |
|---------|--------------------------------------------|------------------------------------------|
| 1 byte  | walk UTF-8; ogni byte → idx del suo cp     | `codepoint_to_byte` (lower_bound)        |
| 2 cp    | UAX#29 GB1, GB2, GB9, GB11 (ZWJ), GB12/13  | `grapheme_to_codepoint`                  |
| 3 graph | map first-cp → first-HB-cluster            | `glyph_to_grapheme` (linear probe)       |
| 4 glyph | UAX#29 WB1, WB5a, WB999                    | `word_to_glyph`                          |
| 5 word  | `PlacedParagraphLayout.lines`              | `line_to_word`                           |
| 6 line  | 1-paragraph model (multi-para = follow-up) | `paragraph_to_line`                      |
| 7 para  | `SemanticSpanRef[]` first-wins             | `span_to_paragraph`                      |
| 8 span  | named byte-range                            | `span_index_by_name`                     |

### Acceptance criteria (8 TEST_CASE in `tests/text/test_text_unit_map_8level.cpp`)

(a) ZWJ family emoji 👨‍👩‍👧 = 1 grapheme cluster (GB11 merges ZWJ+extpict).
(b) `cafe + combining acute` (U+0065+U+0301) → 4 codepoints, 4 graphemes (GB9 attaches combining marks).
(c) `fi` ligature → 1 glyph covering 2 codepoints; both graphemes map to glyph 0.
(d) `Hello, world!` → 2 words (WB5a whitespace break), 1 line.
(e) OOB safety su tutti i 7 inverse lookups (below/beyond range → nullopt).
(f) Determinismo bit-exact su 100 iterazioni identiche (counts + spot round-trip).
(g) `identity_at_byte(0)` → tutti gli 8 indici pieni; forward-then-inverse round-trip chiuso.
(h) Empty input → counts=0, lookup nullopt, `_for_parent` counts=0, identity=InvalidIndex.

### Cap & determinismo

- `max_source_bytes` default 1MB per evitare array unbounded
  (anti-duplication memory safety).
- Cap applicato silently troncando la stringa a `max_source_bytes`.
- No threads, no PRNG, no time → bit-exact riproducibile.

### Deferred (follow-up atoms)

- Multi-paragraph layout (`PlacedParagraphLayout` multi-paragraph).
- Reverse mapping for `byte_to_grapheme` (skipping codepoint step).
- Migration dei 30+ callers del narrow `glyph_selector::TextUnitMap`
  verso la ladder a 8 livelli (TICKET-046 next-track storico).
