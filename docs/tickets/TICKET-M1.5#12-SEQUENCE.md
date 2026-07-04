# TICKET-M1.5#12-SEQUENCE — software_backend.cpp multi-module refactor

> **Sequenced refactor** of `src/backends/software/software_backend.cpp` (357 LOC monolitico) into 5 single-responsibility TUs under `src/backends/software/`.
> Direzione bloccata fino a baseline verde (AGENTS.md v0.1 feature freeze), ma operativamente user-mandated exception.
> Status osservabile per step (PASS/FAIL/PARTIAL); chiusura per commit (uno per step).

## Final architecture (after 4 commits)

| File | Responsabilità unica | LOC stimate |
|---|---|---|
| `software_backend.cpp` | ctor + dtor + move ctor/assign + accessors (counters/framebuffer_pool) + `attach_processor_context()` | ~70 |
| `software_backend_factory.cpp` | `make_software_backend()` factory + UNIQUE validation source (REQUIRED-services null-check) | ~80 |
| `software_backend_dispatch.cpp` | `draw_node()` (processor dispatch via registry) | ~70 |
| `software_backend_text.cpp` | `capabilities()` + `draw_text_run()` (Blend2D text dispatch) | ~110 |
| `software_backend_effects.cpp` | `apply_blur()` + `composite_layer()` + `apply_effect_stack()` + `apply_per_pixel_dof()` + (`to_local_clip`/`clipped_area` helpers + 3 `chronon3d::renderer::...` extern decls) | ~150 |

TOTALE: ~480 LOC (vs 357 originale, +120 per-file header comments + duplication-free specificity).

## Architectural invariants (per user spec)

1. **`make_software_backend()` è l'UNICA fonte di validazione servizi.**  
   Bug-fix: `#ifndef NDEBUG` asserts duplicati presenti nel ctor (fino a prima di questo commit) sono stati RIMOSSI. Il factory asserts (in factory.cpp, sotto `#ifndef NDEBUG`) sono PARTE della validazione.
2. **Zero nuove classi pubbliche, zero nuovi simboli in `include/chronon3d/backends/software/`.**  
   Tutti i metodi pubblici di `SoftwareBackend` restano dichiarati in `software_backend.hpp` (header NON modificato). Le definizioni si spostano semplicemente in TUs diverse (C++ member function definitions hanno accesso ai private members via `this` indipendentemente dalla TU di definizione → nessun `friend` declaration necessario, nessun Pimpl, nessun internal-bridge header).
3. **`tools/check_doc_sync.sh` (gate #7) resta PASS dopo ogni commit** (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS aggiornati per step).
4. **Compilazione STEP-BY-STEP verde** (`chronon3d_backend_software` OBJECT library). Downstream canary: `chronon3d_sdk_impl` (link completo).

## Step-by-step progress (updated per commit)

| Step | Module | Status | Commit SHA | Note |
|---|---|---|---|---|
| **1/4** | `software_backend_factory.cpp` | **DONE** | _filled-in post-commit_ | Factory + UNIQUE validation source. Ctor asserts rimossi (anti-duplication invariant). |
| 2/4 | `software_backend_effects.cpp` | PLANNED | — | apply_blur + composite_layer + apply_effect_stack + apply_per_pixel_dof + anon helpers + extern decls |
| 3/4 | `software_backend_text.cpp` | PLANNED | — | draw_text_run + capabilities |
| 4/4 | `software_backend_dispatch.cpp` + slim down | PLANNED | — | draw_node + final slim of software_backend.cpp to ~70 LOC |

## Anti-drift

- I 4 step DEVONO avvenire in questo ordine (factory primo perché sblocca l'anti-duplication invariant subito).
- Ogni step è un commit atomico su main con `tools/wrap_push.sh origin main`.
- CHANGELOG entry per step (reverse-chrono, OGNI commit ha la sua entry).
- FOLLOWUP_TICKETS row passa da PARTIAL → DONE a step completato.
- CURRENT_STATUS row aggiornato per step.

## Vincoli AGENTS.md v0.1

- **Cat-5 borderline:** `software_backend.hpp` NON viene modificato (nessuna nuova superficie pubblica).  
- **Cat-1 (build corrections) overlap:** nessun nuovo `#include <...>` in public header; nessun nuovo `target_compile_definitions`.  
- **Doc-sync (gate #7):** questo ticket è doc-only, ma il .cpp TU nuovo deve essere listato in CMakeLists prima del commit.  
- **Per-branch rebase:** ogni push passa per `tools/wrap_push.sh origin main`. Divergenza gestita via auto-FF + `git pull --rebase`.

## Causalità

- M1.5#11 (precedente): cleanup di `software_renderer.hpp` (LOC 223→182, gate-3 I2) — ha lasciato `SoftwareBackend` come TU separato ma ancora monolitico.
- M1.5#12 (questo ticket): split il monolito in moduli single-responsibility, mantenendo l'API pubblica invariata.
- M1.5#13 (parallelo): refactor parallelo di `text_preset_registry.cpp` (multi-category factory extraction); entrambi seguono il pattern "single-responsibility extraction senza friend/internal-bridge header".

## Reference material

- `include/chronon3d/backends/software/software_backend.hpp` — header NON modificato.
- `src/backends/software/internal/software_processor_services.hpp` — pattern internal-bridge esistente (NON applicato qui perché member-access pattern non lo richiede).
- `docs/FOLLOWUP_TICKETS.md` — index one-line di questo ticket (riga P1).
- `docs/CHANGELOG.md` — entry per step (Luglio 2026).
- `docs/CURRENT_STATUS.md` — progress row per step.
