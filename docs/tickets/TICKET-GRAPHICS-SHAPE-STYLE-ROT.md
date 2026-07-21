# TICKET-GRAPHICS-SHAPE-STYLE-ROT — Build rot in graphics/shape_style headers

## Stato: PARZIALE DONE (2026-07-21, commit `d0ed0876` + this commit)
- **DONE (GradientStop rot)**: fix in `stroke_style.hpp` (2 call-site) + `fill_style.hpp` (3 call-site) → `::chronon3d::GradientStop`. `chronon3d_animations` rebuild PASS post-fix.
- **OPEN (systemic rot, 19 file rimanenti)**: stesso pattern doubled-namespace in `camera/`, `effects/`, `core/`, `runtime/`, `internal/`, `render_graph/`, `backends/`, `timeline/`, `animations/`, `math/`. Tracciato in [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md).

## Problema

Lo smoke test del verdict CapCut-grade ha scoperto che **tutti i target di test che transitano per `chronon3d_animations`** (cioè transitivamente per `chronon3d_content` → `chronon3d_animations` → `chronon3d_graphics`) **NON compilano dal source HEAD `5518c619`**.

Errore verbatim del compilatore (catena `chronon3d_animations`):

```
include/chronon3d/graphics/shape_style/stroke_style.hpp:114:21:
    'GradientStop' is not a member of 'chronon3d::chronon3d'
include/chronon3d/graphics/shape_style/stroke_style.hpp:115:9:
    'fs' was not declared in this scope
include/chronon3d/graphics/shape_style/stroke_style.hpp:162:21:
    'GradientStop' is not a member of 'chronon3d::chronon3d'
include/chronon3d/graphics/shape_style/stroke_style.hpp:163:9:
    'fs' was not declared in this scope
include/chronon3d/graphics/shape_style/fill_style.hpp:87:5:
    'GradientStop' is not a member of 'chronon3d::chronon3d'
include/chronon3d/graphics/shape_style/fill_style.hpp:92:36:
    template argument 1 is invalid
```

## (a) SHA introduttore

`git log --oneline 7eb5c2ba..HEAD -- include/chronon3d/graphics/shape_style/{stroke,fill}_style.hpp include/chronon3d/graphics/gradient.hpp include/chronon3d/scene/model/shape/fill.hpp` rivela **3 commit** che hanno toccato i file coinvolti:

| SHA | Subject |
|---|---|
| `99fa4983` | refactor(graphics): extract `StrokeStyle` into `stroke_style.hpp` (FASE 9 Step 1) |
| `da8cb70c` | refactor(graphics): extract lerp helpers into `fill_style_lerp.hpp` (FASE 9 Step 2) |
| `1c6e4c88` | fix(graphics): move lerp include outside `chronon3d::graphics` namespace |

**SHA candidato primario**: `1c6e4c88` — spostare l'include di `fill_style_lerp.hpp` **fuori** dal blocco `namespace chronon3d::graphics { ... }` ha cambiato il contesto di name-lookup per `chronon3d::GradientStop` nei call-site che vivono *dentro* il namespace block (linee 114-115 e 162-163 di `stroke_style.hpp`).

**SHA candidato secondario**: `99fa4983` — l'estrazione di `StrokeStyle` ha creato il bridge code in `to_path_stroke()` / `to_shape_stroke()` che usa il legacy `chronon3d::GradientStop` con campo `offset`, mentre il nuovo `chronon3d::graphics::GradientStop` ha campo `position`.

## (b) Impatto source-build vs binary-observed

| Metrica | Stato |
|---|---|
| `cmake --build --target chronon3d_animations` da HEAD | **FAIL** (errori scope resolution `GradientStop` / `fs`) |
| `cmake --build --target chronon3d_text_*_tests` (transitivi) | **FAIL** (propagazione degli stessi errori) |
| Binari pre-costruiti oggi (mtime 2026-07-21 07:10-07:26) | **FRESH** (nessuna source modification locale post-baseline `5518c619` doc-only) |
| ctest su 6 text suite (binary-run, no rebuild) | 3/7 PASS, 3 FAIL timeout (precedenti attesi dal verdict) |

**Gap source vs binary**: i binari esistenti sono stati compilati in uno stato in cui il build era verde (presumibilmente commit precedente a `1c6e4c88`). Da HEAD, ogni `cmake --build` ri-trigger la failure. Questo è esattamente il pattern §Test binary staleness check di AGENTS.md: i binari **non** riflettono lo stato di HEAD.

## (c) Minimal-surface fix proposto

### Root cause analysis

Il namespace `chronon3d` contiene `GradientStop` (legacy, campo `offset`) in `include/chronon3d/scene/model/shape/fill.hpp:17`. Il namespace `chronon3d::graphics` contiene `GradientStop` (FASE 9, campo `position`) in `include/chronon3d/graphics/gradient.hpp:42`. Entrambi esistono contemporaneamente.

Il bridge code in `stroke_style.hpp::to_path_stroke()` / `to_shape_stroke()` (linee 134-135 e 152-153) usa `chronon3d::GradientStop fs; fs.offset = cs.position;` — qualifica il **legacy** perché `GradientFill::stops` (anch'esso in `chronon3d::`) è `std::vector<chronon3d::GradientStop>` legacy.

L'errore `'GradientStop' is not a member of 'chronon3d::chronon3d'` suggerisce che il lookup `chronon3d::GradientStop` dal contesto `chronon3d::graphics::to_path_stroke()` **non trova** la definizione. Possibili cause (in ordine di probabilità):

1. **`1c6e4c88` namespace boundary move**: spostando l'include di `fill_style_lerp.hpp` fuori dal `namespace chronon3d::graphics { ... }` block, l'header `fill_style_lerp.hpp` viene ora processato in un contesto lessicale dove `chronon3d::GradientStop` non è visibile.

2. **`fill_style_lerp.hpp` non include `scene/model/shape/fill.hpp`**: il bridge code si appoggia sull'inclusione transitiva via `stroke_style.hpp` → `shape.hpp` → `fill.hpp`. Se l'ordine di inclusione cambia, la definizione potrebbe non essere più visibile al punto d'uso.

### Opzioni di fix (Cat-3 minimal-surface)

**Opzione 1 (raccomandata, 1-line fix)**: aggiungere `#include <chronon3d/scene/model/shape/fill.hpp>` esplicito in cima a `fill_style_lerp.hpp` *prima* del suo utilizzo dei tipi `chronon3d::GradientStop` / `chronon3d::Fill`.

**Opzione 2 (alternativa)**: qualificare esplicitamente il lookup `::chronon3d::GradientStop` (con doppio `::`) nei 3 call-site del bridge in `stroke_style.hpp` (linee 134, 152) e `fill_style.hpp` (linea 102). Forza il lookup dal namespace globale, aggirando l'ambiguità lessicale.

**Opzione 3 (FASE 9 alignment, più invasiva)**: aggiornare `scene/model/shape/fill.hpp` per rimuovere la definizione legacy di `GradientStop` e fare `using chronon3d::graphics::GradientStop;` dentro `namespace chronon3d`. Rinominare `GradientStop::offset` → `position` nei call-site `Fill::linear/radial/conic()` e `sample_gradient()`. **Cat-3 surface**: tocca header pubblico di `include/chronon3d/` (richiede ADR per giustificare cambio ABI).

**Opzione 4 (revert)**: revert del commit `1c6e4c88` se la sua diagnosi è confermata come trigger. Rischio: potrebbe rompere il pattern AGENTS.md §2×-in-one-chore deprecation-reversal (introdurre un forward-point ticket per preservare l'intento del fix).

### Decisione

Provare **Opzione 1** per prima (1-line, Cat-3 zero-surface su `fill_style_lerp.hpp` che è header nuovo FASE 9). Se non risolve, scalare a **Opzione 2** (3 call-site, sempre Cat-3 minimal). Riservare **Opzione 3** per ADR separato post-fix.

## (d) Test coverage proposta

Per validare il fix in ordine:

1. **Compilazione pulita**:
   ```bash
   cmake --build build/chronon/linux-content-dev --target chronon3d_animations -j2
   ```
   Atteso: exit 0, zero errori `GradientStop` / `fs`.

2. **Smoke ctest 6 suite** (binari aggiornati post-fix):
   ```bash
   ctest --test-dir build/chronon/linux-content-dev \
       -R 'chronon3d_(text_health|text_production_v1|text_golden|text_preset_visual|golden_matrix_subtitle|subtitle_productive)_tests' \
       --output-on-failure --timeout 30
   ```
   Atteso post-fix: 3/7 → 6/7 PASS (i 3 timeout FAILs permangono come fail-mode noti dal verdict bbox-overflow, da risolvere con Fase 1 fix `780580da` già pushed + ri-goldenatura).

3. **Golden matrix FAST_MODE** (riproducibilità):
   ```bash
   CHRONON3D_GOLDEN_MATRIX_FAST_MODE=1 ctest --test-dir build/chronon/linux-content-dev \
       -R 'golden_matrix' --output-on-failure --timeout 30
   ```
   Atteso: stesso comportamento di prima del rot (i 3 sub-test 82-84-85 con failure pre-esistenti dal verdict).

## §Design rationale

- **Perché ora?**: rot scoperto da smoke test del verdict CapCut-grade su `main@5518c619`. Non bloccante per il rilascio V0.1 (i binari esistenti funzionano) ma impedisce qualsiasi futura build incrementale. Se pushed senza fix, il prossimo agente che tenta `cmake --build` ricrea la failure.
- **Perché non hoistato a Fase 1/2 del verdict?**: il verdict CapCut-grade si focalizzava su text/subtitle geometry, non sui graphics headers. Il rot è **cross-cutting** tra le due aree (la Fase 9 stroke-style è stata emessa indipendentemente).
- **Perché include/chronon3d/ e non src/?**: la modifica proposta (Opzione 1) tocca `fill_style_lerp.hpp` che vive in `include/chronon3d/graphics/shape_style/` — header pubblico. Per regola AGENTS.md §Feature freeze "no nuova API SDK", una modifica a header pubblico va giustificata: in questo caso è un fix di un rot esistente, non espansione di API. AGENTS.md Cat-3 zero-surface preservata (no nuovi simboli).

## §Systemic rot expansion (2026-07-21, post Opzione 2 verification)

Il rebuild post-fix Opzione 2 ha rivelato che il rot è **sistemic**, non isolato a GradientStop. Il pattern `chronon3d::chronon3d` (doubled-namespace in clang diagnostic) appare in ulteriori 19 file:

- **camera**: `include/chronon3d/math/camera_2_5d_projection.hpp`, `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp`, `include/chronon3d/scene/camera/camera_v1/camera_program.hpp`
- **effects**: `include/chronon3d/effects/curves.hpp`, `include/chronon3d/effects/glow_pipeline.hpp`, `src/effects/effect_catalog.cpp` (+ missing `#include` per `EffectStack`)
- **core**: `include/chronon3d/core/config.hpp`, `include/chronon3d/core/execution/execution_scope_types.hpp`, `include/chronon3d/core/scheduler/execution_scheduler.hpp`, `include/chronon3d/core/scope/execution_scope.hpp`, `include/chronon3d/core/types/frame_context.hpp`
- **runtime/internal**: `include/chronon3d/runtime/render_runtime.hpp`, `include/chronon3d/internal/runtime/render_session.hpp`, `include/chronon3d/internal/runtime/session_services.hpp`
- **render_graph**: `include/chronon3d/render_graph/builder/precomp_builder_service.hpp`, `include/chronon3d/render_graph/cache/scene_program_cache.hpp`
- **backends**: `include/chronon3d/backends/software/software_renderer.hpp`, `include/chronon3d/backends/software/software_render_session.hpp`
- **timeline**: `include/chronon3d/timeline/composition.hpp`
- **animations**: `include/chronon3d/animations/camera_motion_params.hpp`

**Root cause comune**: stessa architettura FASE 9 (estrazione sotto-namespace) + commit `1c6e4c88` (move lerp include outside `chronon3d::graphics` namespace) che ha cambiato il contesto di lookup per `chronon3d::X` da dentro `namespace chronon3d::subns { ... }`.

**Strategia raccomandata (per TICKET-SYSTEMIC-NAMESPACE-ROT)**: stessa Opzione 2 (qualificazione `::chronon3d::X`) applicata in modo targeted per type, NON regex generico (rischio over-qualification di `chronon3d::subns::Y` legit). Aggiungere `#include` espliciti per tipi non transitivamente visibili (es. `EffectStack` in `effect_catalog.cpp`).

## §Forward-points

- **TICKET-TEXT-BBOX-OVERFLOW** (P1, **DONE in `780580da`**): fix bbox geometrico già pushed; la matrice `golden_matrix_subtitle` va ri-goldenata post-fix in un chore separato.
- **TICKET-GOLDEN-REGEN-POST-CAPCUT-VERDICT** (planned): dopo questo fix + Fase 4 word-binding, rigenerare le golden delle 3 matrici FAIL con `CHRONON3D_UPDATE_GOLDENS=1`.

## §Cronologia

- 2026-07-21: smoke test verdict CapCut-grade su `5518c619` → build rot scoperto (3/7 PASS vs 11/11 baseline verde `7eb5c2ba`).
- 2026-07-21: ticket aperto (this chore).

## References

- Smoke test observation: vedi chat history 2026-07-21 ("Smoke test rapido della baseline attuale" turn).
- AGENTS.md §Test binary staleness check (rule che ha rilevato la divergence).
- AGENTS.md §Feature freeze "no nuova API SDK" (giustificazione per modifica header pubblico come fix rot).
- AGENTS.md §Disciplina di aggiornamento dei canonici (questo TICKET è la cronaca-estesa home).