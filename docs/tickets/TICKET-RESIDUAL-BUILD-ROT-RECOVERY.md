# TICKET-RESIDUAL-BUILD-ROT-RECOVERY — Recovery from post-push PARTIAL green-build state

## Stato

macchina-verificato PARTIAL (2026-07-13, post-commit `e423fa72` fast-forward pull)

## Priorità

P0 (build-rot cascade; 11/11 baseline verde rule per AGENTS.md §regole violated)

## Problema

Build at HEAD `5a7ae534` was RED: `chronon3d_core_tests` 2 errors, `chronon3d_content` 3 errors, `chronon3d_simd_parity_blend_tests` 4 errors + binary missing. AGENTS.md rule "Mantieni baseline verde: 11/11 gate su ogni commit su main" violated. §honesty closure required.

## Red-herring correction (macchina-verifica 2026-07-13)

The 5 forward-points listed as primary in the original ticket body (own commit `28df419` since reset-away pre-pull) were **CASCADE red-herrings** all chained from a single root cause: a duplicate `LayerBuilder& text(std::string name, const TextSpec& spec);` declaration in `include/chronon3d/scene/builders/layer_builder.hpp`. Upstream commit `ee90cb23` (refactor(builders): split LayerBuilder into domain files) introduced a fresh docstring'd declaration (line 408) without removing the legacy F3.D inline-commented variant (line 402). The duplicate is invalid C++ (same-signature overload disambiguation = compile fail) and cascades parser failure to every TU including `layer_builder.hpp` — almost every composition plus the SIMD test transitively.

Upstream commit `8e1b6aed` (fix(recovery): text+camera build cat-fixset, 9 fixes) covered 4 of 7 original content/* rot but did NOT catalog the load-bearing line-402-vs-line-408 duplicate (out-of-scope for that fix-forward batch).

Deleting line 402 (keeping the canonical `////` docstring'd overload at line 408 per AGENTS.md "non duplicare" + ADR-019 §A.2 canonical selection rule) collapsed ALL 5 listed forward-points to N/A red-herring in one line. Build green-restored; SIMD binary produced (564MB at `build/chronon/linux-fast-dev/tests/chronon3d_simd_parity_blend_tests`).

## Evidenza

| Probe | Pre-fix | Post-fix |
|-|-|-|
| `cmake --build` `chronon3d_core_tests` | error: `LayerBuilder::text(std::string, const TextSpec&) cannot be overloaded with LayerBuilder::text(std::string, const TextSpec&)` at `layer_builder.hpp:408` | BUILD SUCCESS (100/100 linking) |
| `cmake --build` `chronon3d_content` | error: same overload at `layer_builder.hpp:408` | BUILD SUCCESS (no-work, cached) |
| `cmake --build` `chronon3d_simd_parity_blend_tests` | error: same overload + 4 cascade errors | BUILD SUCCESS + binary produced 564MB |
| `ctest --test-dir build/chronon/linux-fast-dev -R simd` | (binary missing) | 4/5 TEST_CASEs PASS, 1/5 FAIL (test_simd_parity_blend.cpp:51 scalar alpha=0 identity rot — see TICKET-SIMD-PRECISION-DRIFT) |

## Impatto

- BUILD: green-restored (verbatim 11/11 baseline gate PARTIAL-not-promoted due to simd residual).
- TESTS: 4/5 simd tests PASS — ctest exits 0 only by mask-discovery; the 1/5 fail is a genuine scalar_reference semantic rot that is NOT related to the load-bearing fix.
- SDK API: zero surface change (the orphan was INVALID C++ — produced no symbol; canonical surface unchanged post-dedup).

## Confine

IN SCOPE:
- The `layer_builder.hpp:402` duplicate declaration rot (load-bearing fix-forward).
- Rebuild + macchina-verifica of the 3 named targets.
- Documentation ledger (CHANGELOG Cita-Only + FOLLOWUP_TICKETS row + this ticket body) per docs/DOCUMENTATION_GOVERNANCE.md Cat-5 pattern.

OUT OF SCOPE (forward-pointed):
- The `scalar_blend alpha=0` identity rot — separate ticket TICKET-SIMD-PRECISION-DRIFT (functional semantic rot in the reference impl, NOT rot cascade from the header dup).
- Legacy pre-existing rot WBH-blocked per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent.

## Soluzione accettabile

Single-line header dedup per Cat-3 minimal-surface: delete the F3.D inline-commented duplicate at `layer_builder.hpp:402`; keep the canonical `////` docstring'd "Legacy TextSpec forwarder" overload at line 408 (which maps the legacy substructs into a TextDefinition per ADR-019 §A.2). Zero new SDK API surface, zero new HEADER touched beyond the one line deleted.

## Criteri di accettazione

- [x] All 3 build targets BUILD SUCCESS — VERIFIED 2026-07-13 (chronon3d_core_tests + chronon3d_content + chronon3d_simd_parity_blend_tests).
- [x] SIMD binary produced (`build/chronon/linux-fast-dev/tests/chronon3d_simd_parity_blend_tests` 564MB) — VERIFIED 2026-07-13.
- [x] Red-herring correction narrative recorded in this ticket body — VERIFIED 2026-07-13.
- [x] Cat-3 minimal-surface contract honored (1 file, -1 line, -1 character of comment; zero new SDK symbol) — VERIFIED 2026-07-13.
- [ ] `*simd*` ctest 5/5 PASS — DEFERRED to TICKET-SIMD-PRECISION-DRIFT (functional rot, not build rot; not resolvable in this VPS scope).

## Forward-points (reclassified from original 6)

| Original | Status post-macchina-verifica |
|-|-|
| `[HOTFIX-content-designator-4]` (easy_text_animations.cpp:65 + text_placement_compositions.cpp 4 lines) | N/A — was parser-cascade noise (TextFrame struct order in `text_definition.hpp:91` already matches designator order in content/*) |
| `[HOTFIX-content-syn]` (image_animated_reveals.cpp:145) | N/A — was parser-cascade noise |
| `[HOTFIX-content-conv]` (chronon_glow_final.cpp:175) | N/A — was parser-cascade noise |
| `[SIMD-bin]` (chronon3d_simd_parity_blend_tests binary missing) | macchina-verificato — binary produced post-dedup |
| `[LOAD-BEARING-fix]` (header dedup) | macchina-verificato — single-line Cat-3 fix landed |
| `[HONESTY]` (state disclosure + cat-5 ledger) | macchina-verificato — this ticket + CHANGELOG Cita-Only + FOLLOWUP row |
| **NEW: `[SIMD-PARITY-DRIFT]` (scalar_blend alpha=0)** | ACTIVE forward-point — see TICKET-SIMD-PRECISION-DRIFT |

## Cronaca

- 2026-07-13 (commit `28df419`, since reset-away pre-pull): ticket OPEN with P0 status + 6 forward-points (5 misdiagnosed as primary rot + VERIFY + HONESTY).
- 2026-07-13 (commit `e423fa72` fast-forward pull): local pulled in 2 upstream commits (`e423fa72` chore(docs): codify 2x-in-one-chore deprecation reversal rule + `6254e451` refactor(motion): migrate 11 presets to AnimationTrack<T>). Neither touched `layer_builder.hpp` — duplicate declaration still present post-pull (verified via `git log --name-only HEAD@{1}..HEAD` + `grep -nE 'LayerBuilder& text\(\s*std::string\s+name,?\s*const\s+TextSpec'` shows 2 matches). **This confirms my fix is UNIQUE per the `21ece2b3 unique-edit recovery variant` precedent** (cross-link: AGENTS.md §Post-push SHA-selfcheck invariant) — not semantic-equivalent to upstream.
- 2026-07-13: dropped partial-applied `stash@{0}` (containing pre-pull chore version) + re-applied chore FRESH on post-pull tree. The drop was deliberate: `stash pop` would have triggered a CHANGELOG 78-line rebase conflict against upstream `e423fa72` + `6254e451`; the drop+reapply-fresh path is the canonical Cat-3 anti-dup choreography. Stash list pre/post: `stash@{0}` was the new `c58d0bd6 refactor(content)` entry (preserved from prior session); `stash@{1}` was the older AVX2 stack (preserved).
- 2026-07-13: applied Cat-3 minimal-surface fix — single Python `text.replace(old, new, 1)` on `layer_builder.hpp:402-407` (verbatim exact-match check + sys.exit(1) on miss-provenance).
- 2026-07-13: macchina-verificated — all 3 build targets BUILD SUCCESS + SIMD binary produced + 5 listed forward-points reclassified N/A per red-herring correction + 1 NEW forward-point opened (TICKET-SIMD-PRECISION-DRIFT).

## Cross-references (SHA cites inline per AGENTS.md §SHA cite pattern)

- Upstream commit `ee90cb23` (refactor(builders): split LayerBuilder into domain files) — introduced the load-bearing line-402/408 duplicate via fresh docstring'd declaration.
- Upstream commit `8e1b6aed` (fix(recovery): text+camera build cat-fixset, 9 fixes) — covered 4 of 7 original content/* rot but did not catalog the line-402 dup (out of batch scope).
- Upstream commit `e423fa72` (chore(docs): codify 2x-in-one-chore deprecation reversal rule) — landed pre-pull; **did NOT replicate my fix** (verified post-pull via grep).
- Upstream commit `6254e451` (refactor(motion): migrate 11 presets to AnimationTrack<T>) — landed pre-pull; presets refactor only, no overlap with builder text overload.
- Commit `5a7ae534` (docs(followup): open TICKET-ANIMATED-VALUE-ADD-KEYFRAME-MIGRATION row) — most recent surviving local commit pre-pull; cat-5 ticket pair precedent.
- Commit `28df419` (docs(followup): open TICKET-RESIDUAL-BUILD-ROT-RECOVERY row) — original ticket-open commit, reset-away pre-pull; chore re-applied FRESH on post-pull tree per the precedent established.
- TICKET-SIMD-PRECISION-DRIFT (NEW cat-5 ticket, forward-point anchor for scalar reference rot).
- TICKET-ANIMATED-VALUE-ADD-KEYFRAME-MIGRATION (sibling cat-5 ticket row, opened by 5a7ae534).
- TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (precedent for WBH-DEFERRED macchina-verifica on this VPS).
- AGENTS.md §Post-push SHA-selfcheck invariant — the `21ece2b3 unique-edit recovery variant` precedent applies directly to this chore (post-pull verification confirmed upstream did NOT replicate my fix; re-applied FRESH on post-pull tree).
- AGENTS.md §regole "Mantieni baseline verde: 11/11 gate" — the rule whose §honesty closure this ticket documents (PARTIAL not green).
- AGENTS.md "Cat-3 minimal: prefer DELETE over WRAP" — the discipline that makes single-line dedup the canonical resolution path.
- ADR-019 §A.2 — canonical authoring entry point (F2.C `text(name, TextDefinition)` recommended; F3.D inline-comment duplicate is the rot, not the canonical).
