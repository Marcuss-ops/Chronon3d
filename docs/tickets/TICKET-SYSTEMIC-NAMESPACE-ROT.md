# TICKET-SYSTEMIC-NAMESPACE-ROT — Systemic doubled-namespace rot across 19 files

## Stato: OPEN (2026-07-21, commit pending)

## Problema

Il rebuild post-fix Opzione 2 di [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) ha rivelato che il build rot `'X' is not a member of 'chronon3d::chronon3d'` è **sistemic** (19 file), non isolato al GradientStop.

Errori verificati (verbatim dal compilatore):
```
src/effects/effect_catalog.cpp:16:5: 'stack' was not declared in this scope
include/chronon3d/effects/curves.hpp:27:31: 'CurvePoint' in namespace 'chronon3d::chronon3d' does not name a type
include/chronon3d/core/config.hpp:NN: 'graph' / 'cache' / 'Easing' in namespace 'chronon3d::chronon3d' does not name a type
include/chronon3d/runtime/render_runtime.hpp:NN: 'RenderSession' / 'ExecutionScheduler' in namespace 'chronon3d::chronon3d' does not name a type
... (16 altri file con stesso pattern)
```

## Root cause

Stessa architettura FASE 9 (estrazione sotto-namespace `chronon3d::subns`) + stessa commit `1c6e4c88` (move lerp include outside `chronon3d::graphics` namespace) documentata in [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) §(a).

Quando codice dentro `namespace chronon3d::subns { ... }` fa lookup qualificato `chronon3d::X` (parent namespace), clang formatta l'errore come `'X' in namespace 'chronon3d::chronon3d'` (concatenazione diagnostica del current scope + qualified lookup).

## Scope (0 file)

| Area | File |
|---|---|
| camera | `include/chronon3d/math/camera_2_5d_projection.hpp`, `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp`, `include/chronon3d/scene/camera/camera_v1/camera_program.hpp` |
| effects | `include/chronon3d/effects/curves.hpp`, `include/chronon3d/effects/glow_pipeline.hpp`, `src/effects/effect_catalog.cpp` |
| core | `include/chronon3d/core/config.hpp`, `include/chronon3d/core/execution/execution_scope_types.hpp`, `include/chronon3d/core/scheduler/execution_scheduler.hpp`, `include/chronon3d/core/scope/execution_scope.hpp`, `include/chronon3d/core/types/frame_context.hpp` |
| runtime/internal | `include/chronon3d/runtime/render_runtime.hpp`, `include/chronon3d/internal/runtime/render_session.hpp`, `include/chronon3d/internal/runtime/session_services.hpp` |
| render_graph | `include/chronon3d/render_graph/builder/precomp_builder_service.hpp`, `include/chronon3d/render_graph/cache/scene_program_cache.hpp` |
| backends | `include/chronon3d/backends/software/software_renderer.hpp`, `include/chronon3d/backends/software/software_render_session.hpp` |
| timeline | `include/chronon3d/timeline/composition.hpp` |
| animations | `include/chronon3d/animations/camera_motion_params.hpp` |

## Strategia di fix raccomandata

**Targeted qualification** (NON regex generico che rischia over-qualification):

1. Per ogni file, identificare i tipi specifici che falliscono (es. `CurvePoint`, `EffectStack`, `graph`, `ExecutionScheduler`, `RenderSession`, `AssetRegistry`, `RenderCounters`, `Config`, `ImageCache`, `Scene`, `Easing`).
2. Per ogni tipo T, applicare `chronon3d::T` → `::chronon3d::T` SOLO per i call-site dentro sub-namespace (`namespace chronon3d::subns { ... }`).
3. Aggiungere `#include` espliciti per tipi non transitivamente visibili (es. `effect_stack.hpp` in `effect_catalog.cpp`).
4. NON toccare `chronon3d::subns::Y` (legittimo, NON over-qualificare).

**Magnitude stimato**: ~30-50 LoC totali across 19 file (1-3 modifiche per file).

**Test coverage**:
- `cmake --build build/chronon/linux-content-dev --target chronon3d_animations -j2` → PASS (già verde post-Opzione 2 GradientStop)
- `cmake --build build/chronon/linux-content-dev --target chronon3d_text_health_tests chronon3d_text_production_v1_tests chronon3d_text_golden_tests chronon3d_text_preset_visual_tests chronon3d_golden_matrix_subtitle_tests chronon3d_subtitle_productive_tests -j2` → atteso PASS post-fix sistemico
- `ctest --test-dir build/chronon/linux-content-dev -R 'chronon3d_(text_health|text_production_v1|text_golden|text_preset_visual|golden_matrix_subtitle|subtitle_productive)_tests' --output-on-failure --timeout 30` → atteso 6/7 PASS

## Forward-points

- Dopo questo fix + golden regen post-Fase 1/4 (`TICKET-GOLDEN-REGEN-POST-CAPCUT-VERDICT`), smoke test finale per confermare 11/11 baseline verde.

## §Cronologia

- 2026-07-21: smoke test iniziale → scoperto GradientStop rot (TICKET-GRAPHICS-SHAPE-STYLE-ROT aperto in `d0ed0876`)
- 2026-07-21: Opzione 2 GradientStop fix verificato (chronon3d_animations PASS) → rebuild 6 test targets FAIL con rot sistemico aggiuntivo
- 2026-07-21: questo ticket aperto per tracciare lo scope espanso

## References

- [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) — ticket originale (GradientStop, PARZIALE DONE)
- AGENTS.md §Feature freeze "no nuova API SDK" — il fix è rot-mitigation, non API expansion (giustifica modifica a header pubblici)
- AGENTS.md §Fare PR piccole e mirate — il fix sistemico richiede chore separato per rispettare scope discipline
