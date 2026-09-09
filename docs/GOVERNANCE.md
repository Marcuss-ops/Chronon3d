# Chronon3D — Governance & Guardrail

> **Scopo:** 7 cose da tenere sottochio ogni settimana per evitare che Chronon strasbordi, diventi troppo grosso o perda la rotta.
> **Origine:** sintesi operativa dell'audit 2026-05-11 (storico in `docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md`); stato corrente verificato in `docs/AUDIT-2026-09-09.md`.
> **Policy:** questo file è l'unico posto che definisce gli invarianti non negoziabili e il rituale settimanale. I dettagli tecnici restano nei ticket/ADR.

---

## 1. Tre invarianti non negoziabili

Se uno di questi salta, la rotta è persa. Tutto il resto è riparabile.

| # | Invariante | Come verificarlo |
|---|---|---|
| **I-1** | **Una sola authority per concetto.** `cmake/Chronon3DRegistry.cmake` è legge. Nuovo effect/node/cache/type passa dal registry/resolver/sampler canonico. Mai pipeline parallela. | `tools/check_architecture.py` (regole in `tools/architecture_rules*.toml`) deve restare verde. Grep: nessun secondo `add_library(..._registry)`, nessun `*Registry` duplicato. |
| **I-2** | **RuntimeContext possiede, il resto prende in prestito.** Job possiede `slot/queue/payload/cancellation/counters`, Frame prende in lease. Nessun `new Framebuffer` / `make_shared<Framebuffer>` nel per-frame data-plane. | Audit H-01/H-07 (LAND — `docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md`). Grep per-frame: `make_unique<Framebuffer>` concesso solo in `FramebufferPool` o `EffectScratchResources`. |
| **I-3** | **Preparazione sincrona, esecuzione senza I/O.** Il per-frame non tocca disco/rete/hash file. Tutto il `sha256_file` / `resolve` sta nel barrier (`ResourcePreparation` / `render_preparation`). | Audit C-03 (LAND — `docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md`). Grep `src/render_graph` non deve contenere `sha256_file`, `AssetResolver::resolve`, `fopen`. |

**Definizione di "strasbordo":** qualsiasi PR che aggiunge una feature senza passare per l'authority di I-1, o che introduce allocazione/I-O nel per-frame violando I-2/I-3.

---

## 2. Sette hotspot da monitorare

| # | Hotspot | Dove guardare | Segnale d'allarme | Azione |
|---|---|---|---|---|
| **H-1** | **Frequenza × Complessità** | `git log --stat` + complessità ciclomatica. File critici: `src/runtime/render_pipeline.cpp`, `src/cache/framebuffer_pool.cpp`, `src/runtime/gpu_glyph_atlas_core_detail.hpp`, `src/backends/software/processors/effects/blur/radial_blur.cpp` | File >600 righe toccato >3×/settimana | Priorità assoluta: non aggiungere feature lì, spezzalo. Vedi D-02/D-04 (`docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md`). |
| **H-2** | **Mutex globali sul data-plane** | `FramebufferPool::m_mutex` (`src/cache/framebuffer_pool.cpp:95`), `GpuGlyphAtlas::m_mutex` (`src/runtime/gpu_glyph_atlas_core_detail.hpp:64`), `NodeMemoryTracker::m_mutex` | `m_lock_wait_us` / `m_lock_hold_wall_us` cresce con i thread (`FramebufferPoolStats`) | Sharda prima di parallelizzare. Vedi S-01/S-02/S-03 (`docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md`). |
| **H-3** | **Allocazioni per-frame (heap churn)** | `FrameAllocCounter` (`src/backends/software/internal/frame_alloc_counter.*`), `RenderCounters::framebuffer_allocations` | `framebuffer_allocations > 0` in steady-state a 60fps | Deve essere 0 dopo warmup. Tutto da `FrameSlotPayload::arena` o `EffectScratchResources`. Vedi H-01/H-02/H-03. |
| **H-4** | **Build graph & dimensione binaria** | `cmake/Chronon3DRegistry.cmake` (`CHRONON3D_REGISTRY_OBJECT_LIBS` ~30 OBJECT), tempo build `build/`, `vcpkg_installed/` | +1 dipendenza vcpkg senza ADR, +10% build time / mese | ADR obbligatorio per nuova dipendenza. Vedi U-03/U-04. |
| **H-5** | **Contratti che divergono** | `src/runtime/resource_preparation.cpp` — la failure policy è UNICA (`should_fail_loud`, AUDIT N-03/N-05: anche la fase mesh passa dal predicato condiviso, `CorruptedAsset` su asset required ora è fail-loud). Resta aperto solo C-02 (`prepare_layouts` keyato su `Image`; richiede `AssetKind::Layout` — cambio di contratto pubblico, vedi AUDIT-2026-09-09 §deferral) | `WARN` in `WarnAndSkip` produce frame nero senza errore loud | Fail-loud al barrier. Vedi C-01/C-02/E-04. C-03 (doppio `sha256_file`) **LAND**. |
| **H-6** | **Duplicazione di conoscenza (DRY)** | `include/chronon3d/runtime/gpu_glyph_atlas.hpp` — unify completato (AUDIT U-01): anche `glyph_key_hash64` usa `core::hash::HashBuilder`; non resta alcun combine manuale in quel header | Cambi una regola e tocchi 4 file (Shotgun Surgery) | Unifica su `HashBuilder`/`xxHash` (già dipendenza `chronon3d_base`). Vedi U-01/U-02 — **LAND**. |
| **H-7** | **Dead code & fallback legacy** | D-03 era un **falso positivo**: `ResourcePlanTelemetry` è consumata dall'exporter CLI (`apps/chronon3d_cli/utils/job/render_job_finalize.cpp:112-122`) e `peak_live_bytes` alimenta la `CompiledResourceTable`. `lowered_into_batch` non è dead: è il flag del fusion pass con validazione (`frame_graph_builder_program_detail.hpp:254-256`) | `grep TODO/FIXME` cresce (oggi: 0), `E-01` glyph invisibile su OOM (oggi: fail-loud, **LAND**) | Ogni ingresso abilita un'uscita (Demolition Debt in `AGENTS.md`). |

**Come prioritizzare (matrice Frequenza × Complessità):**

|  | Bassa frequenza | Alta frequenza |
|---|---|---|
| **Alta complessità** | Priorità media (toccare solo se si rompe) | 🔥 **PRIORITÀ ASSOLUTA** — refactoring immediato |
| **Bassa complessità** | Priorità bassa (lasciare così) | Priorità fluida (manutenzione ordinaria) |

---

## 3. Tre segnali che la rotta è già persa

1. Proponi una feature e la risposta è *"dove la metto?"* invece di *"quale authority la possiede?"* → confini rotti (I-1).
2. `git diff` di una feature tocca `runtime/` + `render_graph/` + `backends/software/` insieme → accoppiamento.
3. Aggiungi `if (fallback) { /* silently */ }` senza metrica/log → stai nascondendo il problema (E-01/E-04).

---

## 4. Rituale settimanale (30 min)

| Quando | Comando / check | Criterio verde |
|---|---|---|
| **Lunedì** | `git log --oneline --since="1 week" --stat \| head -40` + `git diff --stat HEAD~20` | Sai quali 3 file sono più toccati. Se uno è hotspot H-1, apri ticket. |
| **Mercoledì** | Apri `docs/FOLLOWUP_TICKETS.md` + `docs/AUDIT-2026-09-09.md` (P0 residui: C-01, C-02, S-05). Quanti chiusi questa settimana? | ≥1 mosso, o motivazione scritta in `docs/FOLLOWUP_TICKETS.md`. |
| **Venerdì** | `python tools/check_architecture.py` + `bash tools/check_architecture_boundaries.sh` + `bash tools/gates/manifest.sh` (se presente) | Tutto verde. Su `NOT RUN` non apri release. |

**Regola d'oro:** ogni nuova feature deve lasciare **meno duplicazione di quella che ha trovato** (AGENTS.md: "No duplicazione" + Demolition Debt). Se non ci riesci, la feature è disegnata male, non è il codice.

---

## 5. Riferimenti

| Documento | Ruolo |
|---|---|
| `docs/AUDIT-2026-09-09.md` | Audit corrente (stato codice 2026-09-09) |
| `docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md` | Log bonifiche audit (D-01..E-07, stati LAND/DEFER, task G-01..G-11) |
| `AGENTS.md` | Invarianti permanenti, cache taxonomy, no-duplication, workflow git, Demolition Debt |
| `docs/CURRENT_STATUS.md` | Stato presente per area (PASS/FAIL/NOT RUN) |
| `docs/RELEASE_GATE.md` | Requisiti permanenti di release |
| `tools/architecture_rules.toml` | Regole architetturali dichiarative (engine: `tools/check_architecture.py`) |
| `cmake/Chronon3DRegistry.cmake` | Unica authority build graph (OBJECT libs + SDK deps) |
| `include/chronon3d/cache/cache_taxonomy.hpp` | 3 famiglie cache (nessun'altra) |

---

*Governance approvata: 2026-05-11. Aggiornata: 2026-09-09 (audit `docs/AUDIT-2026-09-09.md`: H-5 policy unificata, H-6 unify completato, H-7 D-03 rettificato a falso positivo; `FIXING.md` e `docs/chronon-wave-closeout-todo.md` ritirati, cronaca in `docs/tickets/`). Prossima review: quando un P0 aperto cambia stato, o a ogni baseline verde (`docs/baselines/main-<sha>-baseline.md`).*
