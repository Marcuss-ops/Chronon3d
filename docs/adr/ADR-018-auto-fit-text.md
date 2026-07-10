# ADR-018 — Auto-fit text: shrink-only binary search on `compile_text_layout()` with 12-iteration bound

| Field | Value |
|---|---|
| **Status** | ✅ Accepted |
| **Date** | 2026-07-10 |
| **Deciders** | Text pipeline architecture owner |
| **Tags** | `text`, `auto-fit`, `font-size`, `binary-search`, `layout`, `compile_text_layout`, `ParagraphStyle` |
| **Related** | `include/chronon3d/text/paragraph_style.hpp` (field declarations); `src/text/text_run_builder.cpp` (`compile_text_layout` canonical compiler); `src/scene/builders/text_run_builder.cpp` (`compile_or_cache_layout` materializer); `include/chronon3d/text/text_run_builder.hpp` (`TextLayoutRequest` / `TextCompileServices`); ADR-001 (frame-graph-compiler, layout pipeline); `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` (text V1 roadmap) |

## Context and scope

Chronon3d's text pipeline (`compile_text_layout()` → `TextRunLayout`) compiles a `TextDocument` + `TextLayoutSpec` into shaped + positioned glyphs.  The `TextLayoutSpec::box` field defines the available bounding box (width × height in pixels).  When the authored text overflows the box — too many glyphs, too large a font, too narrow a wrap width — the layout simply overflows: glyphs render past the box boundary with no diagnostic and no correction.

Motion-graphics workflows (subtitles, lower thirds, auto-scaling titles) require the text to **fit** the box automatically.  After Effects' "Auto-fit text" feature shrinks the font size until the paragraph fits; Premiere Pro's "Responsive Design" does the same.  The common UX contract is: "the text must never overflow the box; shrink the font if needed, but never grow beyond the authored size."

`ParagraphStyle` already declares three fields for this feature (added in TEXT-PLY-01):

```cpp
bool auto_fit_font_size{false};
f32  min_font_size{8.0f};
f32  max_font_size{96.0f};
```

These fields are **declared but unused** — no code path reads them.  This ADR specifies the algorithm, the search strategy, and the integration point.

---

## Decision 1 — Shrink-only (not shrink+grow)

### Spec

Auto-fit is **shrink-only**: when `auto_fit_font_size == true`, the compiler may **reduce** the effective font size to make the paragraph fit the box, but it must **never increase** it beyond the authored font size (`TextLayoutSpec::font.font_size`).

The authored font size is the **ceiling** of the search range.  `max_font_size` is an upper bound clamp (default 96.0f) that caps the ceiling when the authored size exceeds it — but the compiler never *grows* toward `max_font_size`; it only uses it as a guard.

The search range is therefore:

```
low  = min_font_size                    (default 8.0f)
high = min(authored_font_size, max_font_size)
```

If `high <= low` (e.g. authored font is smaller than `min_font_size`), auto-fit is a no-op.

### Rationale

**Shrink+grow** (also called "fit-to-box" in InDesign) would increase the font size when the text underflows the box.  This creates three problems:

1. **Oscillation risk**: In a per-frame pipeline where the box size or text content changes incrementally, grow-then-shrink cycles can produce visible font-size jitter across adjacent frames.  The binary search converges, but the *authored* font size is no longer a stable anchor.

2. **Author intent violation**: Motion-graphics artists set a specific font size for visual hierarchy.  Growing beyond it breaks the design — a 24pt subtitle that auto-grows to 36pt because the box is wide looks wrong.

3. **Complexity**: Shrink+grow requires two passes (first try grow, then shrink if grow overshoots) or a ternary search with a "natural size" pivot.  Shrink-only is a monotonic binary search — simpler, faster, deterministic.

Shrink-only matches After Effects' "Auto-fit text" behavior and Premiere Pro's "Responsive Design — Auto-size" contract.  It is the expected behavior in the motion-graphics domain.

### Failure mode (if violated)

A regression that grows the font size would produce visible size changes across frames in compositions where the box size animates (e.g. a lower-third that slides in from off-screen).  The text would snap between font sizes as the box crosses the "fits naturally" threshold.  No test lock exists yet for shrink-only enforcement — this ADR specifies the contract; the test lock is a follow-up.

---

## Decision 2 — Binary search on `compile_text_layout()` at 12 iterations

### Spec

The auto-fit algorithm is a **binary search** over the font size range `[min_font_size, min(authored_font_size, max_font_size)]`.  Each iteration calls `compile_text_layout()` at the candidate font size and checks whether the resulting `TextRunLayout::bounds` fits within the box.

**Iteration count: 12** (fixed, not adaptive).

12 iterations produce 2^12 = 4096 discrete steps.  For a typical range of 8pt–48pt (40pt span), this gives ~0.01pt precision — far below the perceptual threshold (~0.5pt at 1080p).  For the full default range of 8pt–96pt (88pt span), precision is ~0.02pt — still sub-perceptual.

```
function auto_fit_font_size(params, engine, box):
    authored = params.text.font.font_size
    low  = params.paragraph_style.min_font_size       // default 8.0
    high = min(authored, params.paragraph_style.max_font_size)  // default min(32, 96) = 32

    if high <= low:
        return compile_text_layout(params, engine)    // no-op

    best = nullptr
    for i in 0..11:                                   // 12 iterations
        mid = (low + high) / 2.0
        candidate = params with .text.font.font_size = mid
        layout = compile_text_layout(candidate, engine)
        if layout.bounds.width <= box.width && layout.bounds.height <= box.height:
            best = layout                              // fits — try larger
            low = mid
        else:
            high = mid                                 // overflows — try smaller

    return best ?? compile_text_layout(params with .font_size = low, engine)
```

### Why 12 iterations (not adaptive)

| Alternative | Problem |
|---|---|
| **Adaptive (convergence tolerance)** | Non-deterministic iteration count → cache-key instability.  Two calls with the same input could converge in 8 or 11 iterations depending on floating-point rounding, producing slightly different `mid` values.  The `TextLayoutCacheKey` would differ, causing cache misses. |
| **Fixed 8 iterations** | 2^8 = 256 steps → ~0.35pt precision over 88pt range.  Visible at 1080p for large text (32pt+).  Not sufficient for typographic quality. |
| **Fixed 16 iterations** | 2^16 = 65536 steps → overkill.  Each iteration calls `compile_text_layout()` which runs HarfBuzz shaping.  16 iterations = 16 shaping passes per text run per frame.  At 30fps with 10 text runs, that's 4800 shaping calls/sec just for auto-fit.  12 iterations (3600/sec) is the sweet spot. |
| **Linear scan (step 0.5pt)** | 80 steps over 40pt range.  80× more shaping calls than binary search.  Unacceptable for real-time. |

12 iterations is **deterministic** (same input → same sequence of `mid` values → same cache keys), **precise** (sub-perceptual), and **bounded** (max 12 `compile_text_layout()` calls per auto-fit text run per materialization).

### Integration point

The binary search runs inside `compile_or_cache_layout()` (`src/scene/builders/text_run_builder.cpp`), **before** the cache lookup.  The rationale:

1. The cache key must include the **resolved** (auto-fitted) font size, not the authored size.  If the cache key used the authored size, two calls with different box sizes but the same authored font would collide on a stale layout.

2. The binary search itself is NOT cached — only the final result is.  The intermediate `compile_text_layout()` calls use `cache=nullptr` (same pattern as the existing materializer bypass) to avoid polluting the cache with intermediate candidates.

3. The final auto-fitted layout is stored in the cache with a key that includes the resolved font size + box dimensions.  Subsequent frames with the same box+text hit the cache directly.

### Source anchor (planned)

`src/scene/builders/text_run_builder.cpp` — inside `compile_or_cache_layout()`, between the cache-key construction and the `compile_text_layout()` call.  The auto-fit block inserts a candidate font size into `cache_key.font_size` before the cache lookup.

### Test lock (planned)

- **Shrink triggers**: A test with `auto_fit_font_size=true`, `min_font_size=8`, authored font 48pt, and a narrow box (200px wide) asserts that the resolved font size is < 48pt and the bounds fit the box.
- **No-op when fits**: A test with `auto_fit_font_size=true` and a wide box asserts that the resolved font size equals the authored size (no shrink).
- **Min clamp**: A test with `min_font_size=12` and a very narrow box asserts that the resolved font size is ≥ 12pt (never shrinks below minimum).
- **12-iteration determinism**: A test runs auto-fit 100 times with the same input and asserts all 100 resolved font sizes are bit-identical.

---

## Decision 3 — Cache-key integration: resolved font size replaces authored size

### Spec

When auto-fit is active, the `TextLayoutCacheKey` uses the **resolved** (auto-fitted) font size, not the authored size.  This ensures:

- Two text runs with the same authored font but different box sizes produce different cache keys (correct — they have different layouts).
- Two text runs with the same box size and same text hit the cache (correct — the auto-fit converges to the same font size).

The cache key additionally includes `box_width` and `box_height` (already present in the key), so the box dimensions are part of the lookup.

### Rationale

If the cache key used the authored font size, a text run that auto-fits to 24pt in a narrow box would cache a layout at 24pt.  A subsequent call with a wider box (where 32pt fits) would hit the cached 24pt layout — wrong font size, wrong layout.

Using the resolved font size means the binary search runs on every **first** materialization (cache miss), but subsequent frames with the same box+text hit the cache directly (cache hit).  The cost is 12 `compile_text_layout()` calls on the first frame — acceptable for a one-time materialization cost.

---

## Decision 4 — Per-frame driver integration: auto-fit runs at materialization time only

### Spec

Auto-fit runs **once** at materialization time (`materialize_text_run_shape` → `compile_or_cache_layout`).  The per-frame driver (`update_text_run_shape_per_frame`) does **not** re-run auto-fit.  If the box size changes between frames, the materialized shape retains the font size from the first materialization until a full re-materialization is triggered.

### Rationale

Re-running 12 `compile_text_layout()` calls per text run per frame would be prohibitively expensive (see Decision 2 — 3600+ shaping calls/sec).  The materialization-time-only contract matches the existing pipeline: `compile_or_cache_layout()` runs once per unique `TextLayoutCacheKey`, and the per-frame driver only evaluates animators (position, opacity, scale) — it never re-shapes text.

If the box size animates (e.g. a lower-third sliding in), the text re-materializes when the cache key changes (different `box_width`/`box_height`).  This is the existing behavior for all layout-affecting parameters — auto-fit is not special here.

### Failure mode (if violated)

A regression that re-runs auto-fit per frame would cause visible font-size jitter as the binary search converges to slightly different values across frames (floating-point rounding).  The cache would miss on every frame (different intermediate `mid` values), defeating the purpose of caching entirely.

---

## Consequences

### Positive

* **Deterministic**: 12 fixed iterations → same input → same output → stable cache keys.
* **Sub-perceptual precision**: ~0.02pt over the default 8–96pt range.  Invisible at any target resolution.
* **Bounded cost**: Max 12 `compile_text_layout()` calls per auto-fit text run per materialization.  One-time cost, amortized across all subsequent frames via the cache.
* **Matches industry convention**: Shrink-only matches After Effects / Premiere Pro behavior.  Artists expect this contract.
* **Minimal API surface**: No new public types.  `ParagraphStyle::auto_fit_font_size`, `min_font_size`, `max_font_size` are already declared.  The compiler reads them; no new fields needed.

### Negative / Migration cost

* **12 shaping calls on first materialization**: For a composition with 10 auto-fit text runs, the first frame pays 120 extra `compile_text_layout()` calls.  At ~0.1ms per call (HarfBuzz shaping + glyph placement), this is ~12ms — noticeable but acceptable for a one-time cost.  Subsequent frames are cache-hit (zero cost).
* **No shrink+grow**: Artists who want "fit-to-box" (grow to fill) need a separate feature.  This ADR explicitly excludes grow behavior.
* **Per-frame box-size change**: If the box size animates every frame (e.g. a continuously resizing window), the text re-materializes every frame with 12 iterations.  This is expensive but unavoidable without a more sophisticated approach (e.g. incremental font-size adjustment from the previous frame's value).

### Neutral

* No new public API symbols.  The `ParagraphStyle` fields already exist.
* No new dependencies.  `compile_text_layout()` is the existing canonical compiler.
* No changes to the `TextLayoutCache` or `LruCache` internals.

---

## Alternatives considered

### A. Shrink+grow ("fit-to-box")

Rejected — oscillation risk, author-intent violation, complexity.  See Decision 1 rationale.  A future ADR may introduce a separate `auto_fit_mode` enum (`ShrinkOnly` | `FitToBox`) if grow behavior is needed.

### B. Adaptive convergence (while loop until bounds fit)

Rejected — non-deterministic iteration count causes cache-key instability.  Two calls with the same input could converge in 8 or 11 iterations, producing slightly different `mid` values and different cache keys.  The cache would miss on every frame.

### C. Linear scan (step 0.5pt from max to min)

Rejected — 80 steps over a 40pt range.  80× more shaping calls than binary search.  Unacceptable for real-time rendering.

### D. Analytical approximation (estimate font size from glyph metrics)

Considered — compute the expected text width at a given font size using average glyph advance widths, then solve for the font size that fits the box.  Rejected — inaccurate for mixed-script text (Latin + CJK + Arabic), variable-width fonts, and complex layout (wrap, hyphenation, justification).  The binary search is exact (it uses the actual layout engine) and only 12 iterations.

### E. Per-frame incremental adjustment (EMA from previous frame's font size)

Considered — instead of binary search from scratch each materialization, start from the previous frame's resolved font size and adjust incrementally.  Rejected — introduces inter-frame state (the previous font size), which breaks the stateless materialization contract.  The cache already handles repeated frames (same box+text → cache hit → zero cost).  Incremental adjustment would only benefit the case where the box size changes every frame, which is rare and expensive regardless.

---

## References

* `include/chronon3d/text/paragraph_style.hpp` — `auto_fit_font_size`, `min_font_size`, `max_font_size` field declarations (TEXT-PLY-01).
* `include/chronon3d/text/text_run_builder.hpp` — `compile_text_layout()` signature, `TextLayoutRequest`, `TextCompileServices`.
* `src/text/text_run_builder.cpp` — `compile_text_layout()` implementation (7-stage pipeline).
* `src/scene/builders/text_run_builder.cpp` — `compile_or_cache_layout()` (materializer-side cache + compile delegation).
* `include/chronon3d/text/text_run_layout.hpp` — `TextRunLayout::bounds` (the overflow check target).
* `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` — text V1 roadmap (auto-fit is a V1 milestone).
* ADR-001 — frame-graph-compiler (layout pipeline context).
* ADR-013 — camera_v1 semantics (ADR format precedent).
