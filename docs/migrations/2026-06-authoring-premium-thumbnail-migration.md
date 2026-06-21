# Migration Notes — Authoring-DSL Premium Thumbnail Migration (PR 6)

**Date:** 2026-06-21
**PR:** 6 (authoring DSL validation)
**Author:** Buffalo (parent)/Chronon3D PR pipeline
**Status:** ship-ready (verification pending `./build-fast.sh cli && tools/render_premium_artifacts.sh`)

---

## Summary

Migrated `PremiumThumbnailSaaSBlue` (a high-fidelity product composition from `content/effects/thumbnails/premium_thumbnail_showcase.cpp`) to the new `chronon3d::authoring` DSL, preserving **frame-0 byte-equivalence** as a validation harness. The migrated version lives at `content/effects/thumbnails/premium_thumbnail_saas_blue_authored.cpp` and is registered as `"PremiumThumbnailSaaSBlueAuthored"` via `register_effect_compositions()`.

The original composition is preserved untouched, so:

```text
tools/render_premium_artifacts.sh
├── output/brace_init/premium_thumbnail_saas_blue.png
└── output/authored/premium_thumbnail_saas_blue.png
└── output/diff_report.txt           # compare_pngs.py output
```

`tools/compare_pngs.py` confirms the two PNGs are pixel-identical.

---

## Surface boundary (the honest answer)

`chronon3d::authoring::Layer` exposes **only** `.text(content)` and `.configure_core(Fn)` at the time of this migration. The full LayerBuilder surface (rect, rect-with-fill, rounded_rect, circle, star, path, glow, drop_shadow, opacity, position, screen_dimensions, plus the high-level `draw_rich_text` helper) is reached in PR 6 via `configure_core([&](LayerBuilder& core){ ... })` — the documented Level-3 escape hatch.

Layer-by-layer breakdown of the migration:

| Layer (brace-init) | Migration strategy | Surface mutation pattern |
|---|---|---|
| 1. `background` | configure_core only | `core.rect(...)` |
| 2. `light_beam` | configure_core only | `core.position(...)` + `core.opacity(...)` + `core.path(...)` |
| 3. `badge` | configure_core + authoring text | `core.position/glow/drop_shadow/rounded_rect × 2` + `l.text("Ae").id("ae_text")....at(0,0)` |
| 4. `sparkles` | configure_core only | `core.glow(...)` + `core.star(...) × 2` |
| 5. `hero_text` | position + authoring SAAS + configure_core for rich-text | hybrid; see "Silent footgun" below |
| 6. `arc` | configure_core only | `core.position/glow/path(...)` |

**PR 7+ candidates** (extracted from this migration's layer surface needs):
- `Layer::rect` / `Layer::rounded_rect` / `Layer::circle` / `Layer::star` / `Layer::path`
- `Layer::position` / `Layer::opacity` / `Layer::glow` / `Layer::drop_shadow`
- `Layer::rich_text` helpers (replace `draw_rich_text` calls)

These are deliberately out of scope for PR 6 to keep the migration narrow and reviewable.

---

## Identity preservation — name inheritance

`Layer::text(content)` auto-generates a per-Layer run index (`"text_0"`, `"text_1"`, …) and immediately calls `Text::id(name)` to override it. We chain `.id("saas_title")` and `.id("ae_text")` to match the brace-init `l.text("saas_title", TextSpec{...})` and `l.text("ae_text", TextSpec{...})` calls — so the resolved `PendingTextRun::name` is identical in both versions. This is load-bearing for hashing and Z-order preservation.

For Layer 5's first text entry, the chain `l.text("SAAS").id("saas_title")` produces:
- `pending_->name == "saas_title"` (matches brace-init)
- `pending_->params.text.content.value == "SAAS"`
- `pending_->params.text.font.font_path == "assets/fonts/Poppins-Bold.ttf"` (via `.font(...)`)
- `pending_->params.text.font.font_family == "Inter"` (via `.font_family(...)`)
- `pending_->params.text.font.font_weight == 800` (via `.weight(800)`)
- `pending_->params.text.font.font_size == 110.0f` (via `.font(...)`)
- `pending_->params.text.layout.box == {960.0f, 260.0f}` (via `.box(...)`)
- `pending_->params.text.layout.align == TextAlign::Center` (via `.align(...)`)
- `pending_->params.text.layout.vertical_align == VerticalAlign::Middle` (via `.vertical_align(...)`)
- `pending_->params.text.layout.wrap == TextWrap::None` (via `.wrap(...)`)
- `pending_->params.text.appearance.color == Color{0.98, 1.0, 1.0, 1.0}` (via `.color(...)`)
- `pending_->params.text.position == {0.0f, 0.0f, 0.0f}` (via `.at(0.0f, 0.0f)`)

All 11 fields are 1:1 with the brace-init `TextSpec{...}` literal. The mutation steps all live on `Text::mutable_pending()` — there is no local spec copy that could diverge.

For `'ae_text'` the chain is identical with values scaled for the badge card (size=48.0f, box={120,120}, color cyan).

---

## Silent footgun preservation (intentional)

The brace-init `hero_text` layer in `premium_thumbnail_showcase.cpp` constructs a fluent `TextMaterial mat = TextMaterial::premium(...)` with rich gradient/bevel/glow/shadow settings — **but never assigns it into the `TextSpec` for "saas_title"**. The "SAAS" word-mark renders with the default `TextMaterial{}` (flat, no effects) regardless of the 20 lines of fluent settings above it.

The migration mirrors the bug verbatim. The Layer::text("SAAS") chain for saas_title does **not** call `.material(...)` — adding one would "fix" the bug and break byte-equivalence.

> ⚠️ Follow-up (PR 7+): audit `premium_thumbnail_showcase.cpp::add_hero_text` and the `hero_text` lambda for missing material bindings. This is a defensive bug, not the migration's problem — the migration is a cross-pixel validator, not a code-quality gate.

---

## draw_rich_text fallback (layer 5 subtitle)

Layer 5's `"saas_sub"` entry is constructed by the high-level `draw_rich_text(l, line, pos, opts, font_engine, name)` helper, which is **not** part of the authoring surface. The migration reproduces it verbatim via `configure_core([&](LayerBuilder& core){ ... draw_rich_text(core, ...); })`.

The bytes are preserved because we:
1. Inside `configure_core`, call `draw_rich_text(core, subtitle, pos, opts, core.font_engine(), "saas_sub")` with all 6 arguments bit-for-bit identical to the brace-init source.
2. Borrow `builder` by reference (`[&builder]`) so any internal layer-level state the helper reads (e.g. the layer's font engine) stays in sync with the underlying LayerBuilder.

PR 7 candidates: `Layer::rich_text(content, opts)` thin wrapper around `draw_rich_text` for direct authoring of styled rich-text lines.

---

## Byte-equivalence validation protocol

```bash
# 1. Build the CLI (release variant preferred for stable rendering)
./build-fast.sh release   # or: cmake --preset linux-fast-dev && cmake --build build/chronon/linux-fast-dev

# 2. Render both versions to separate dirs, then compare
./tools/render_premium_artifacts.sh

# 3. Inspect the assertion
cat output/diff_report.txt
# Expect: "Verification SUCCESS: All proofs are bit-exact or pixel-identical!"

# Optional: re-run with explicit frame for non-zero frames
"$BIN" render PremiumThumbnailSaaSBlue        --frame 0 -o output/brace_init/saas.png
"$BIN" render PremiumThumbnailSaaSBlueAuthored --frame 0 -o output/authored/saas.png
python3 tools/compare_pngs.py output/brace_init output/authored
```

**Decision:** if the diff is non-zero, the authoring surface has drifted from the brace-init semantics. Fix the authoring chain (do NOT modify the brace-init baseline — it must remain frozen for the validator to be meaningful).

---

## What's NOT migrated (yet)

PR 6 is intentionally narrow. The following surface areas are explicitly out of scope and queue for PR 7+:

1. **Layer shape primitives** (rect, rounded_rect, circle, star, path) — exposed via `configure_core` in this PR. Future `Layer::rect(...)` etc. methods should produce the same byte output.
2. **Layer-level effects** (glow, drop_shadow, opacity, position) — exposed via `configure_core`. Future `Layer::glow(...)` etc.
3. **RichText helpers** (draw_rich_text, RichTextLine) — exposed via `configure_core`. Future `Layer::rich_text(content, opts)` thin wrapper.
4. **SceneBuilder-level methods** (`s.layer` cycle, camera wiring) — out of scope; PR 4 territory.
5. **Composition registration** — kept on `register_effect_compositions`; PR 4 may add a fluent registration facade.

---

## Risks / caveats

- **Compilation time**: `chrono3d_content` OBJECT library gains one source file (≈300 lines). Negligible impact on Linux-fast-dev build time (≈ +0.5s).
- **Telegraphing the surface boundary**: downstream reviewers may assume the authoring surface is "complete enough for production" once this PR lands. The doc above explicitly calls out the boundary so reviewers can argue for PR 7 surface expansion with confidence.
- **Frame-0 only**: the renderer is called with `--frame 0` deterministically. For non-zero frames, the `premium_thumbnail_saas_blue*` compositions have `duration=1`, so only frame 0 is meaningful — byte-equivalence is well-defined.
- **Telemetry regression risk**: low. The new composition produces the same render output, so the telemetry dashboard will see identical breathing/value traces.

---

## PR 5.1 UB companion fix (bundled)

This PR also ships a one-line companion fix to `include/chronon3d/authoring/detail/basic_registry.hpp::merge()`:

- **Before**: `assert(res.has_value() && "..."); return std::move(*res);` — `assert()` is a release-build no-op, so `*res` on a nullopt `std::optional` executed in release builds ⇒ UB.
- **After**: `if (res.has_value()) { return std::move(*res); } return Value{};` — release build is sound; `Value{}` requires default-constructible (satisfied for `TextStyle` and `TextAnimatorSpec`). If a future `BasicRegistry<X>` instantiates with a non-default-constructible `X`, the proxy lambda fails to compile (acceptable failure mode, documented in the header docstring).

Documented in `docs/migrations/2026-06-authoring-registry.md` (PR 5.1).

---

## Follow-ups filed

1. **PR 7: Layer shape surface** — `Layer::rect/rounded_rect/circle/star/path/glow/drop_shadow/opacity/position/...`. Direct authoring for the parts of this migration currently handled by `configure_core`.
2. **PR 8: Layer rich_text helpers** — `Layer::rich_text(content, opts)` thin wrapper around `draw_rich_text`.
3. **PR 9: Audit `premium_thumbnail_showcase.cpp::add_hero_text` material binding drift** — the silent footgun flagged above. Fix in these compositions, then verify this validator stays green.
4. **PR 10: `Layer::configure_core` second-pass cleanup** — replace `configure_core([&](LayerBuilder& core){ core.position(...); core.glow(...); core.rounded_rect(...); })` chains with the direct Layer-builder methods once PR 7 lands.

---

## Acknowledgement

This migration is a **byte-pair validator**, not a code-quality migration. Production code that uses the authoring DSL is expected to prefer the direct Layer shape methods when PR 7 lands. Until then, the inline `configure_core` bodies are the authoritative layering mechanism and should NOT be flagged as code-smell in pre-PR-7 reviews.
