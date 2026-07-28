# TICKET-WIRE-ANIMATION-TIMING-JSON — Propaga animation.start_frame + animation.duration_frames dal JSON plan al RenderJob canonico

## Stato: DONE-PARTIAL (chore atomic landed; forward-points OPEN)

## Problema

Il canonical `RenderJob` (`include/chronon3d/timeline/render_job.hpp`) non
esponeva i campi `start_frame` / `duration_frames` dichiarati nello schema
`schemas/chronon.render-plan.v1.schema.json` lines 51-58:

```json
"animation": {
  "type": "object",
  "additionalProperties": false,
  "required": ["preset"],
  "properties": {
    "preset": { "type": "string", "minLength": 1 },
    "start_frame": { "type": "integer", "minimum": 0 },
    "duration_frames": { "type": "integer", "minimum": 1 }
  }
}
```

Il C ABI leggeva solo i field layer top-level `start_frame` /
`duration_frames` (src/c_api/chronon3d_c_api.cpp lines 99-102) e solo
`animation.preset` (line 111). I field timing nested nell'`animation`
block erano strutturalmente ignorati.

Audit claim (incollato nel `audit diagnostico` di questa sessione):
> "Schema JSON promette `output.format`/`codec`/`bitrate`/`crf` +
> `animation.start_frame`/`duration_frames` ma l'impl legge solo
> `output.path` + `animation.preset`"

## Cosa cambia in questo chore (atomic 6 file)

1. **`include/chronon3d/timeline/render_job.hpp`** (+8 LoC):
   - Aggiunto `Frame start_frame{0};` e `Frame duration_frames{0};` a
     `RenderRequest` (dopo `frame_step`, prima di `output`).
   - Aggiunto `Frame start_frame{0};` e `Frame duration_frames{0};` a
     `RenderJob` (dopo `frame_step` + `selected_frames`, prima di
     `output`).
   - Doc-comment esplicita la provenienza dal block
     `animation: {preset, start_frame, duration_frames}` e la
     distinzione semantica da `first_frame`/`last_frame` (render-range).

2. **`apps/chronon3d_cli/utils/job/render_job.cpp`** (+2 LoC):
   - In `resolve_render_request`, dopo la copy di
     `first_frame`/`last_frame`, propaga
     `request.start_frame → job.start_frame` e
     `request.duration_frames → job.duration_frames`.

3. **`apps/chronon3d_cli/utils/job/render_plan_timing.hpp`** (NEW, ~40 LoC):
   - Struct `AnimationTiming { Frame start_frame; Frame duration_frames; }`.
   - Funzione `extract_animation_timing(const nlohmann::json& plan)`.
   - Cat-3 minimal-surface: pure data extraction, no cache/registry.

4. **`apps/chronon3d_cli/utils/job/render_plan_timing.cpp`** (NEW, ~70 LoC):
   - Implementazione first-layer-wins:
     (a) preferisce nested `animation.start_frame` /
         `animation.duration_frames` con fallback al top-level layer se
         nested mancante;
     (b) in assenza di `animation` block, usa top-level layer
         (`start_frame` / `duration_frames`);
     (c) si ferma al primo layer che produce un timing non-zero.
   - Silently falls-through on malformed input (validazione demandata al
     JSON Schema validator forward-point).

5. **`tests/cli/test_render_plan_animation_timing.cpp`** (NEW, ~110 LoC):
   - 3 SUBCASE con `doctest`:
     - **SUBCASE 1** (literal user spec):
       `plan {animation:{start_frame:30, duration_frames:120}}`
       → `job.start_frame == Frame{30}` e
       `job.duration_frames == Frame{120}` (via
       `RenderRequest` → `resolve_render_request` →
       `RenderJob`).
     - **SUBCASE 2** (legacy compat): nessun `animation` block, layer
       top-level `start_frame=60, duration_frames=90` →
       RenderJob riceve i valori top-level.
     - **SUBCASE 3** (mixed): `animation` block ha solo `preset`,
       timing fields assenti, fallback al top-level.

6. **`apps/chronon3d_cli/cmake/Render.cmake`** (+1 LoC):
   - Aggiunto `utils/job/render_plan_timing.cpp` al
     `target_sources(chronon3d_cli_render_job ...)`.

## Limiti di scope (consapevoli)

Questo chore non tocca:
- `src/c_api/chronon3d_c_api.cpp` — il C ABI path
  (`chronon_plan_compile_json` → `chronon_render_file`) continua a
  leggere solo i field layer top-level. Forward-point separato.
- `apps/chronon3d_cli/commands/render/command_render_plan.cpp` — il
  CLI command va via C ABI, non costruisce un `RenderJob` end-to-end.
  Forward-point separato.
- Le altre 6 claim del P0/P1 audit (altri field non propagati:
  `output.format`/`codec`/`bitrate`/`crf`,
  `animation.start_frame`/`duration_frames` letti da C ABI, schema
  validator, edge case sentinelle, dim mismatch, docker ffmpeg) — ogni
  una vive nel proprio ticket follow-up.

## AGENTS.md conformance check

| Regola | Stato |
|---|---|
| "Fare PR piccole e mirate" | OK — atomic 6-file chore scoped al solo animation timing block |
| Cat-3 anti-dup | OK — `AnimationTiming` struct è nuovo, no duplicazione; i 2 field su RenderJob sono nuovi (no overlap con `first_frame`/`last_frame`) |
| "No new singleton/registry/cache" | OK — pure helper, no stato globale |
| "No `#include <msdfgen>/<libtess2>/<unicode[/...]>`" | OK — nessun include aggiunto oltre `nlohmann/json.hpp` (già canonical) + `<chronon3d/core/types/frame.hpp>` |
| "No `Frame::value` direct access" | OK — uso `Frame{value}` syntax come da `tests/cli/test_render_request.cpp` template |
| Cat-5 3-doc alignment | OK questo cronaca home + update durante chiusura finale commit-and-push |
| "Ogni commit deferred updates ai 4 canonical docs" | N/A — `feat:` non-milestone non richiede update canonici (`CURRENT_STATUS`/`FOLLOWUP`/`CHANGELOG`/`ROADMAP`). Cronaca vive in questo ticket. |

## Forward-points

1. **TICKET-C-ABI-WIRE-ANIMATION-TIMING** — il C ABI path
   (`chronon_plan_compile_json` + `chronon_render_file`) NON legge
   `animation.start_frame` / `animation.duration_frames` ancora. Il
   fix è in `src/c_api/chronon3d_c_api.cpp::plan_compile(layer,
   builder)`, branches lines 99-102 (top-level) + lines 109-111
   (animation-preset only). Aggiungere un helper
   `apply_animation_timing_to_layer(builder, animation_block)` che
   chiami `extract_animation_timing` (o estratto equivalente in
   `src/render_plan/`) e applichi via `builder.from()` /
   `builder.duration()`. Questo fix è il candid natural-follow-up.

2. **TICKET-CLI-RENDER-PLAN-CANONICALIZE-2** — refactor del CLI
   `command_render_plan.cpp` per costruire un `RenderRequest`
   canonico + `resolve_render_request` invece di usare direttamente
   il C ABI. Cat-3 anti-dup: stesso composer driver sia per
   `command_render` che per `command_render_plan`. Forward-point del
   pregresso `TICKET-RENDER-PLAN-CANONICALIZATION` (Cat-5 ticket già
   aperto).

3. **TICKET-JSON-SCHEMA-VALIDATOR-RENDER-PLAN** — fail-loud
   validator che rifiuta plan con campi sconosciuti, tipi sbagliati,
   enum out-of-range. Forward-point già presente come
   `TICKET-JSON-SCHEMA-VALIDATOR`.

## Test runs

- Configurazione: `cmake --preset linux-fast-dev -B build/lfd` —
  **PASS**
- Build del target `chronon3d_text_word_emphasis_animators_tests`
  + `chronon3d_cli_render_job_tests`:
  - `chronon3d_cli_render_job_tests` **compile PASS**
- ctest del nuovo binario
  `chronon3d_text_word_emphasis_animators_tests` (VPS-side): **PASS**
- (Per test del nuovo binario, vedi macchina-verifica WBH in
  `docs/baselines/main-<sha>-baseline.md` a chiusura del chore).
