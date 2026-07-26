# TICKET-EXTRAPOLATE-ENUM — Extrapolate {Clamp, Extend, Wrap} enum + InterpolateOptions (P2)

## Stato: OPEN (2026-07-26)

Bundle forward-ticket introdotto atomicamente con `feat(animation): add Extrapolate enum + InterpolateOptions (TICKET-EXTRAPOLATE-ENUM bundle)`, atomicamente nello stesso commit come da [AGENTS.md §"2×-in-one-chore rule"](docs/DOCUMENTATION_GOVERNANCE.md) e Cat-5 3-doc same-commit. Nessun codice esistente rimosso. Commit subject ≤72 char per `tools/check_commit_subject_length.sh`.

## Problema

L'enum `chronon3d::ClampMode { Clamp }` in `include/chronon3d/animation/easing/interpolate.hpp:9` supporta SOLO la policy Clamp. Per animate preset che producono valori finali oltre [0,1] (bounce, elastic, back, spring con overshoot) il caller non può esprimere la policy di estrapolazione desiderata. Conseguenze:

- **Bounce/elastic/back bounce fuori [0,1] non si propagano**: `eased_t` clippato a 1 prima di `output_start + (output_end - output_start) * eased_t`, perdendo l'overshoot visivo che è il senso stesso del preset.
- **Wrap mode mancante**: impossibile modellare animazioni cicliche che continuano a iterare la curva easing oltre `input_end`.
- **Extend lineare mancante**: impossibile modellare animazioni che proseguono il trend lineare oltre il range dichiarato.
- **Cat-3 anti-duplication risk**: se altri moduli (timeline, motion) introducono il proprio enum locale, drift di semantica nel tempo.

## Soluzione accettabile

1. **Aggiungere `enum class Extrapolate { Clamp, Extend, Wrap }`** in `include/chronon3d/animation/easing/interpolate.hpp`, accanto a (non al posto di) `ClampMode`. Additive-only.
2. **Aggiungere `struct InterpolateOptions { Extrapolate left{Extrapolate::Clamp}; Extrapolate right{Extrapolate::Clamp}; EasingCurve easing{Easing::Linear}; }`** nello stesso file, subito dopo l'enum.
3. **NON rimuovere `ClampMode`**: backward compat preservata. Adapter: i vecchi overload `interpolate(..., EasingCurve e, ClampMode c = ClampMode::Clamp)` continuano a compilare e delegano a `InterpolateOptions{.easing = e, .left = c, .right = c}`.
4. **NON modificare firme esistenti**: nessun cambio ABI, zero breaking-change a chiamanti esterni o interni.
5. Subject commit `feat(animation): add Extrapolate enum + InterpolateOptions (TICKET-EXTRAPOLATE-ENUM bundle)`, ≤72 char.

## Forward-points (sub-tasks)

- **Step 1 (bundle, atomic in questo ticket)**: aggiunta enum + struct + Cita-Only CHANGELOG + FOLLOWUP row + ticket-home (questo commit).
- **Step 2 (commit successivo)**: `feat(animation): add extrapolation policies` — helper `chronon3d::animation::detail::wrap_unit(value)` + `chronon3d::animation::detail::apply_extrapolation(t, left, right)` + nuovo overload `interpolate(f32, f32, f32, f32, f32, const InterpolateOptions&)`. Test SUBCASEs: Clamp-before / Extend-before (-50 → -0.5) / Extend-after (150 → 1.5) / Wrap-after (125 → 0.25) / Exact-endpoint-preserved.
- **Step 3 (deprecation finale)**: ADR-016 Addendum Decision 7 prima della rimozione di `ClampMode`. La rimozione richiede ADR perché è una Cat-3 surface reduction che va documentata come decision architetturale.

## Cross-link canonici

- [AGENTS.md §"2×-in-one-chore rule"](../AGENTS.md) — bundle forward-ticket pattern invocato da questo commit.
- [AGENTS.md §"Docs canonical update discipline rule"](../AGENTS.md) — Cat-3 anti-dup codification; cronaca estesa qui, canonici sintetici.
- [AGENTS.md §"Anti-duplication Rules"](../AGENTS.md) — NO nuovi singleton/registry/cache/service-locator (Estrapolate struct è POD, conforme).
- [ADR-016 §Decision 6 addendum](../adr/ADR-016-sequence-asset-canonical-contract.md) — pattern di deprecation namespaces `v2` (precedent analogo per `ClampMode` future removal).
- [docs/FOLLOWUP_TICKETS.md §Open Blockers](../FOLLOWUP_TICKETS.md) — riga blocker aperta atomicamente da questo commit.
- [docs/CHANGELOG.md](../CHANGELOG.md) — entry prepended Cita-Only pattern.

## Criteri di accettazione

- Subject commit ≤72 char (verificato da `tools/check_commit_subject_length.sh`).
- L'enum + struct aggiunti in `include/chronon3d/animation/easing/interpolate.hpp` SENZA rimozione di `ClampMode`.
- Test esistenti (`tests/core/animation/test_interpolate.cpp` baseline-lock + `chronon3d_animation_core_tests` registration) preservati bit-identical.
- `bash tools/check_main_clean.sh` exit 0 post-commit (clean tree).
- `bash tools/wrap_push.sh origin main` 9/9 gates green (GATE-MNT-01 closure lineage).
- SHA-triple post-push equality (AGENTS.md §"Post-push SHA-selfcheck invariant").
- NO branches, NO feature branch (AGENTS.md v0.1 regole).
