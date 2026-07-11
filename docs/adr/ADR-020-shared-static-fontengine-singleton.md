# ADR-020 — Shared static FontEngine singleton for 5 typewriter variants

- **Status:** Superseded (PR WP-9 PR 9.0 closure)
- **Date:** 2026-07-10 (Accepted) → 2026-07-10 (Superseded by PR WP-9 PR 9.0)
- **Snapshot:** `main@95594653` — 2026-07-10. Linux-only.
- **Supersedes:** — (does not supersede any prior ADR)
- **Superseded by:** `feat(runtime): FontEngine-from-runtime — plumb through FrameContext + retire shared_typewriter_engine` (commit **`6f3db3ed`**) — this ADR's "How to migrate away" plan IS the implementation; both `shared_typewriter_engine()` and `s_typewriter_resolver` were retired in that same commit (verified-absent in current `main` per `rg -n 'shared_typewriter_engine\|s_typewriter_resolver' content/`); the 5 typewriter lambdas + `anim_typewriter` now source from `ctx.runtime->font_engine()` with graceful fallback to `ctx.font_engine`.

> MADR-style ADR (sections: Context & Forces · Decision · Consequences · Compliance & Verification · References).

## Context & Forces

### Why this ADR exists now

AGENTS.md §"Regole permanenti" explicitly forbids new singletons /
registries / resolvers / caches without an ADR:

> **No nuovi singleton/registry/resolver/cache senza ADR.**

Code-reviewer issue #7 (round 6) flagged that
`content/examples/text/text_animations.cpp::shared_typewriter_engine()`
introduces a new singleton (function-local static `AssetResolver` +
`FontEngine`) that mirrors but does not reuse the existing
`anim_typewriter()` pattern in `content/animation_compositions.cpp`.
The new function-local static is referenced by the 5 typewriter
variants (`AnimTypewriterSimple`, `AnimTypewriterCursor`,
`AnimTypewriterSlide`, `AnimTypewriterGlow`, `AnimTypewriterStagger`)
via `s.font_engine(&shared_typewriter_engine())` — 5 call sites
added in the same commit as the chore cleanup
(`aae298e7 chore(text): cleanup include + diagnostic format`).

The canonical pattern (`anim_typewriter` at
`content/animation_compositions.cpp:66–88`) predates the freeze
(visible in the green baseline `main@7eb5c2ba` which is the
freeze-revocation point). It uses a function-local static
`AssetResolver` mounted at `std::filesystem::current_path()` and
passes the resolver directly to `text::typewriter_build(s, "tw",
{...}, ctx.frame, s_typewriter_resolver)`. The new
`shared_typewriter_engine` is a near-duplicate that additionally
wraps a `FontEngine` and exposes it as a singleton via
`SceneBuilder::font_engine()`.

### The leak in question

`SceneBuilder::font_engine(engine)` is a setter that wires an
external `FontEngine*` onto the scene. The renderer's internal
FontEngine (created per-renderer with the correct resolver) is the
intended wire. The 5 new typewriter variants do NOT use the
renderer's internal FontEngine — they bypass it via the
function-local static. This works because:
- The static is mounted at `current_path()` so `Poppins-Regular.ttf`
  is findable in the project root.
- The static is created once, on first use, before any rendering
  happens, so it's ready when each composition's lambda fires.

The risk: if the renderer has a custom asset mount (e.g. a packaged
distribution where `current_path()` does NOT contain the fonts),
the function-local static will fail to find fonts and the
typewriter compositions will silently render blank text (the
fail-loud path was added for the `build_text_reveal_line` code path
inside the typewriter — the shared engine construction itself does not
fail loudly on missing fonts, the FontEngine just produces zero glyphs
which is then caught by the new throw in `layout_glyphs`).

### Forces in tension

1. **AGENTS.md singleton ban (Regole permanenti):** Adding a new
   singleton requires an ADR. Without one, the function-local
   static in `shared_typewriter_engine` violates the rule.
2. **5 typewriter variants need a FontEngine:** The new
   typewriter variants (simple/cursor/slide/glow/stagger) use
   `build_text_reveal_line` which requires a FontEngine on the
   SceneBuilder. The renderer's internal FontEngine is not
   accessible from the content-layer composition lambda.
3. **Anti-duplication rule (AGENTS.md §5):** The new
   `shared_typewriter_engine` duplicates the
   `s_typewriter_resolver` pattern from `anim_typewriter`. A
   second copy is technically a duplication; an authoritative
   single source of truth would be better.
4. **Invasiveness of the alternative:** The cleanest fix is to
   expose the renderer's FontEngine via the composition ctx
   (e.g. `ctx.runtime.font_engine()` or similar). This requires
   plumbing through `FrameContext` + `composition()` + the runtime
   layer, which is a non-trivial API extension.

### Constraint set

- **AGENTS.md CONSENTITO during feature freeze (REVOCATO 2026-07-06):**
  All categories now allowed. The freeze is revoked; this ADR
  simply acknowledges the technical debt.
- **AGENTS.md permanent rule:** No new singleton/registry/resolver
  without an ADR. This ADR satisfies that requirement.
- **Code-reviewer verdict (round 6, issue #7):** Flag the new
  singleton; require this ADR; defer the refactor that would
  eliminate the singleton until all 5 typewriters can be moved to
  the renderer's FontEngine-from-runtime path.

## Decision

We **accept the new `shared_typewriter_engine()` function-local
static singleton** as a **deferred remediation** — the technical
debt is acknowledged, the rationale is documented, and the
refactor that would eliminate the singleton is deferred to a
follow-up cycle. Specifically:

1. **Author this ADR** documenting the rationale for the new
   singleton. The comment in `shared_typewriter_engine()` is
   updated to point to this ADR (a one-line `// See docs/adr/...`
   link).
2. **Keep the function-local static pattern** for now. The pattern
   is well-tested (the canonical `anim_typewriter` uses the same
   approach since baseline `7eb5c2ba`) and removing it requires
   the FontEngine-from-runtime plumbing which is out of scope for
   this commit cycle.
3. **Defer the elimination refactor.** When the
   FontEngine-from-runtime path is plumbed through `FrameContext`,
   all 5 typewriter variants (and `anim_typewriter`) should be
   migrated to consume the runtime's FontEngine, eliminating
   both `s_typewriter_resolver` and `shared_typewriter_engine`
   in one sweep.

### Why ADR + duplicate pattern, not (a) or (b)

- **Route (a) `refactor(text): plumb FontEngine from runtime to
  composition ctx`** — non-trivial: requires changes to
  `FrameContext` + `composition()` + the runtime layer + the
  content-layer lambda signature. The cleanest path but the
  biggest blast radius; out of scope for the cleanup batch this
  ADR belongs to.
- **Route (b) `fix(content): make the 5 typewriter variants
  consume the renderer's FontEngine directly`** — impossible from
  the content layer: the renderer's FontEngine is not reachable
  from inside a composition lambda. The render-time wiring is
  intentionally opaque to the content layer (separation of
  concerns). The composition lambda only sees `FrameContext`.
- **Route (c) (chosen) `docs(adr): document the new singleton +
  link the comment + accept the duplication`** — minimum-touch
  path that satisfies AGENTS.md (ADR for new singleton), preserves
  the 5 typewriter variants, and defers the architectural fix to
  a dedicated cycle.

## Consequences

### Positive

- The 5 typewriter variants work in any CLI invocation that has
  `assets/fonts/Poppins-Regular.ttf` reachable from
  `std::filesystem::current_path()`. This is the typical
  development and CI environment.
- The fail-loud path in `layout_glyphs` (commit
  `3b833565 feat(text): pre-render invariants — fail-loud instead
  of silent blank`) catches the case where the static cannot
  find the font and produces a `std::runtime_error` with the
  exact `font_path` + `text` snippet in the diagnostic.
- This ADR satisfies AGENTS.md §"Regole permanenti" — the new
  singleton is documented in a canonical ADR file.

### Negative

- **Technical debt:** The `shared_typewriter_engine` is a
  singleton that bypasses the renderer's internal FontEngine.
  This duplicates the `s_typewriter_resolver` pattern from
  `anim_typewriter`. When the FontEngine-from-runtime path is
  plumbed, BOTH patterns can be eliminated.
- **Layering concern:** The content layer (authoring +
  composition) is reaching down into the engine/runtime layer
  (engine = harfbuzz + asset lookup; the FontEngine is a runtime
  resource owned by the renderer/scene-builder, not the content
  authoring layer). Architecturally the FontEngine should be
  sourced from the runtime (a `ctx.runtime.font_engine()` API or
  similar), not owned by content-layer code. The current pattern
  blurs the layering: `content/examples/text/text_animations.cpp`
  embeds engine-side concerns (`FontEngine` + `AssetResolver`
  mount) into the content module, which means the content module
  is no longer purely authoring+composition. The proper fix is the
  FontEngine-from-runtime plumbing described in the migration
  plan below; this ADR defers that refactor but explicitly
  acknowledges the layering violation as the primary motivator.
- **Path sensitivity:** The function-local static mounts at
  `std::filesystem::current_path()`. If a future CLI invocation
  changes the working directory between mount and use, the static
  will not pick up the new path. Mitigation: the static checks
  `has_mount()` before mounting, so a second call to the function
  in a different cwd will not re-mount. This is a known limitation
  of the pattern, accepted with the duplication.
- **No constructor validation:** The function-local static
  constructs `FontEngine(s_resolver)` eagerly on first use. If
  the resolver fails to mount (e.g. permission denied on cwd),
  the exception propagates from the first composition invocation
  that uses it. Mitigation: the throw is in `layout_glyphs` and
  produces a diagnostic; the user sees the exact `font_path` and
  can fix the cwd.

### Neutral

- The new function-local static does NOT introduce a new
  global mutable state at namespace scope (it's a function-local
  static, per the C++ "magic statics" pattern which is
  thread-safe by the C++11 standard).
- The pattern is identical to the canonical `anim_typewriter`
  pattern from the green baseline. No new conventions are
  introduced.

## Compliance & Verification

### How to audit

```bash
# 1. Verify the singleton is documented in this ADR
git log --all --oneline | grep 'ADR-020'
# Expected: a commit referencing ADR-020

# 2. Verify the comment in shared_typewriter_engine points to this ADR
grep -A 1 'shared_typewriter_engine' content/examples/text/text_animations.cpp | head -3
# Expected: comment line "// See docs/adr/ADR-020-shared-static-fontengine-singleton.md..."

# 3. Verify the 5 typewriter variants consume the singleton
grep -c 'shared_typewriter_engine' content/examples/text/text_animations.cpp
# Expected: 6 (1 definition + 5 callers)

# 4. Verify the canonical pattern in anim_typewriter predates the freeze
git log --all --oneline content/animation_compositions.cpp | head -10
# Expected: the anim_typewriter pattern commit visible in the green
# baseline 7eb5c2ba

# 5. Verify the fail-loud path is active
grep -c 'layout_glyphs:' content/common/text_reveal_helpers.hpp
# Expected: 1 (the throw is in place)
```

### How to migrate away (future work, not this commit)

```bash
# When FontEngine-from-runtime is plumbed, the migration is:
# 1. Add ctx.runtime.font_engine() to FrameContext
# 2. In all 5 typewriter lambdas, replace:
#      s.font_engine(&shared_typewriter_engine());
#    with:
#      s.font_engine(&ctx.runtime.font_engine());
# 3. Delete shared_typewriter_engine() and the comment-link to this ADR
# 4. Migrate anim_typewriter() similarly
# 5. Both s_typewriter_resolver and shared_typewriter_engine disappear
# 6. Update this ADR with a "Superseded by" line + close the ticket
```

## References

- `content/examples/text/text_animations.cpp:shared_typewriter_engine` (the new singleton)
- `content/animation_compositions.cpp:66–88` (the canonical `anim_typewriter` pattern, originally introduced in commit **`2ba38c78`** as the `static const AssetResolver` form, then modified in commit **`d4737889`** — mount-pattern modification (removed `const`, added lazy-mount guard at `std::filesystem::current_path()`); both visible in the green baseline `main@7eb5c2ba`)
- `content/common/text_reveal_helpers.hpp:layout_glyphs` (the fail-loud throw)
- `AGENTS.md` §"Regole permanenti" — singleton ban
- `AGENTS.md` §5 — anti-duplication rule
- `docs/baselines/main-7eb5c2ba-baseline.md` — the green baseline that contains the canonical pattern
- `docs/adr/ADR-012-chronon3d-sdk-manifest-boundary.md` — example of "Accepted (deferred action)" MADR-style ADR
- `docs/DOCUMENTATION_GOVERNANCE.md` — ADR template + style guide
- Code-reviewer-minimax-m3 issue #7, round 6

- Commit **`2ba38c78`** (original `s_typewriter_resolver` introducer — `static const chronon3d::assets::AssetResolver s_typewriter_resolver;` declaration + use-site in `text::typewriter_build(..., s_typewriter_resolver)`; see commit message for context on replacing sed residuals `s_test_resolver`)
- Commit **`6f3db3ed`** (retirement commit — both `s_typewriter_resolver` and `shared_typewriter_engine()` deleted; supersedes this ADR's "How to migrate away" plan)
