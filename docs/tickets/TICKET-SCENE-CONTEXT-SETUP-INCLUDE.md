# TICKET-SCENE-CONTEXT-SETUP-INCLUDE — 3rd-level onion-peel rot-cascade (resolve_scene_camera + NodeCacheKey-persists)

## Stato: CLOSED (2026-07-13, rot-cascade resolved upstream at commit `4791e98b` — `src/render_graph/pipeline/scene_context_setup.cpp` at upstream already includes `#include "helpers.hpp"` (line 20) and calls `resolve_scene_camera(scene)` correctly; local fix superseded)

## Origine

Questo ticket è stato aperto durante Azione 18 chore cycle dopo che il
Phase B-2 rot-fix (4-file `#include <chronon3d/cache/framebuffer_pool.hpp>`
canonical include add) ha **apparentemente sbloccato** la compilazione
dei link layer ma ha esposto un **3rd-level rot** completamente distinto
in `src/render_graph/pipeline/scene_context_setup.cpp`.

## Multi-layer rot-cascade (3-deep onion-peel)

| # | Layer | Ticket | Stato (post rot-fix) |
|---|---|---|---|
| 1 | Brace rot (`AnimatedValue<f32>{...}` initializer missing `}` × 3) | TICKET-TEXT-PRESET-SELECTORS-BRACE | rot-fix applied in working tree via str_replace (3 siti `};` → `}};` su `line_sel.end` / `word_sel.end` / `grapheme_sel.end`) |
| 2 | NodeCacheKey + FramebufferPool incomplete-type rot (cannot convert int) | TICKET-RENDER-GRAPH-CACHEKEY-INCOMPLETE-TYPE | rot-fix PARZIALE: `framebuffer_pool.hpp` canonical include ADD in 4 headers (= `node_runner.hpp` / `execution_state.hpp` / `node_skip_policy.hpp` / `executor_levels.hpp`); FramebufferPool siti cleared. NodeCacheKey persistenza TBV (LLM-basher summary suggests errors still presenti in `node_runner.cpp:35:18`, `execution_state.hpp:59:12`, `compiled_frame_graph.hpp:38:12`, `node_skip_policy.cpp:41:44`) — richiede `cmake --build target=chronon3d_core_tests 2>&1 | grep -E ': error:' verbatim` per conferma |
| 3 | **resolve_scene_camera undeclared in scene_context_setup.cpp:51** | **THIS TICKET** | scoperto post Phase B-2 rot-fix. `scene_context_setup.cpp` include solo `scene_context_setup.hpp` + `camera_change_policy.hpp`; manca `#include "helpers.hpp"` (dove `resolve_scene_camera` è definito a `src/render_graph/pipeline/helpers.hpp:168`) |

## Problema (Phase 3 rot)

```
src/render_graph/pipeline/scene_context_setup.cpp:51:34: error: 'resolve_scene_camera' was not declared in this scope; did you mean 'resolved_camera'?
```

Call site a linea 51:
```cpp
const auto resolved_camera = resolve_scene_camera(scene);
```

`resolve_scene_camera(const Scene&)` è definito in `helpers.hpp:168` come:
```cpp
[[nodiscard]] inline ResolvedCamera resolve_scene_camera(const Scene& scene) { ... }
```

Il call site NON include `helpers.hpp` (solo `scene_context_setup.hpp` + `camera_change_policy.hpp`). Risultato: il linker vede il call site prima di vedere la definizione e segnala "not declared".

Site consumers di `resolve_scene_camera` (altri call site compilati OK perché includono correttamente `helpers.hpp`):
- `src/render_graph/preflight/preflight.cpp:51`
- `src/render_graph/pipeline/debug.cpp:36`, `:73`

(Questi 3 call site funzionano perché includono helpers.hpp transitively o direttamente.)

## Decisione di riparazione (next-session cycle)

Cat-3 minimal-surface, 1-line:
- Aggiungere `#include "helpers.hpp"` a `scene_context_setup.cpp` (path relativo al file, matching pattern di `camera_change_policy.hpp`).
- Confermare via `cmake --build build/manual-test --target chronon3d_core_tests` rc=0.

## Verifica addizionale richiesta (Phase 2 status)

Per Cat-3 + "no silent rot disappearance" discipline, **è necessario un basher
verbatim** per confermare lo status del NodeCacheKey rot dopo il 4-file
framebuffer_pool.hpp fix:

```bash
cmake --build build/manual-test --target chronon3d_core_tests 2>&1 \
  | grep -E ': (error|fatal error):' | head -20
```

Se persistono errori `NodeCacheKey` presso `execution_state.hpp:59` /
`node_runner.cpp:35` / `compiled_frame_graph.hpp:38`, sarà necessario
un fix aggiuntivo (probabilmente: `using chronon3d::cache::NodeCacheKey;`
all'inizio di `namespace chronon3d::graph` in execution_state.hpp +
node_runner.cpp + compiled_frame_graph.hpp, per disambiguare dal
forward-decl namespace shadow).

## Criteri di accettazione per chiusura ticket

1. **Phase 3 (resolve_scene_camera)**: cmake --build Stage 2 deve passare senza error "not declared in this scope" presso scene_context_setup.cpp:51.
2. **Phase 2 (NodeCacheKey conferma)**: stesso build deve passare senza error "NodeCacheKey does not name a type" presso execution_state.hpp:59 / node_runner.cpp:35 / compiled_frame_graph.hpp:38 / node_skip_policy.cpp:41.
3. macchina-verifica deferred a working build host per AGENTS.md §honest-limitation.

## Forward-points & cross-link

- **Precedente onion-peel (4-deep precedent)**: TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT — 4-commit cascade eviction (chore commits e04e8eb7 + a00b9d2f + 95900bde + fbd7483a). Questo caso è 3rd-deep, ma dimostra che la rot-cascade può essere iterata atomicamente.
- **AGENTS.md §Post-push SHA-selfcheck invariant**: snapshot pre-push requirement su ogni rot-cleared commit.
- **AGENTS.md §Diagnosi rule: "non iterare rebase a terzo tentativo"**: il boundary discipline di questo turno è di NON continuare la rot-cascade fix (perché 3-LEVEL cascade supera il green-baseline premise dello "Full delivery on main" plan scelto dall'utente). HALT + defer a next session con questo ticket + i 2 rot-fixes preliminari in working tree come audit trail.
- **Discipline docs**: questo ticket NON aggiorna `docs/FOLLOWUP_TICKETS.md` (perché i rot-fixes preliminari in working tree non sono committed; FOLLOWUP update happens at commit time per AGENTS.md §Disciplina di aggiornamento dei canonici). Il prossimo session che chiude la rot-cascade dovrà aggiungere la row in §Open Blockers.

## Origine

Ticket aperto durante Azione 18 chore cycle (regression lock per il
silent failure di `content/animation_compositions.cpp::anim_typewriter`)
quando il Phase B-2 rot-fix (4-file framebuffer_pool.hpp canonical
include) ha esposto il 3rd-level rot non correlato:

Cadenza onion-peel 2026-07-13:
- **Pre-Phase 2** (pre-session): cmake --build fail a 5 siti NodeCacheKey + FramebufferPool rot (per TICKET-RENDER-GRAPH-CACHEKEY-INCOMPLETE-TYPE §Cadenza).
- **Phase 2** (this session): rot-fix 4-file `#include <chronon3d/cache/framebuffer_pool.hpp>` add in executor/. FramebufferPool siti cleared (per code-reviewer verdict). NodeCacheKey rot parzialmente risolto — richiede verbatim-basher per conferma TBV.
- **Phase 3** (this session): cmake --build fail nuovamente at `scene_context_setup.cpp:51:34: 'resolve_scene_camera' was not declared`, completely different rot-class (missing include). Nuova Cat-5 ticket needed.

## HALT disposition rationale (2026-07-13 this turn)

Per AGENTS.md §`Mantenere baseline verde: proibito push RED` + §`Disciplina di aggiornamento dei canonici` (non aggiornare canonical per fix piccoli che non cambiano stato di area) + il §`non iterare rebase a terzo tentativo` boundary analog (3-LEVEL rot cascade supera il green-baseline premise del `Full delivery on main` plan):

1. **NON** commit + push questo turno (build RED anche dopo Phase 2 rot-fix).
2. **NON** aprire un commit `fix(scene_context_setup): include helpers.hpp` senza conferma verbatim di Phase 2 (NodeCacheKey rot status TBV — rischio di ulteriore rot).
3. **SÌ** questo ticket Cat-5 come audit trail della rot-cascade-discovery + working tree `str_replace` rot-fixes non committed + Azione 18 deliverables (test_anim_typewriter_error_path.cpp + 2 cmake + script + 4 cat-5 tickets) intatti come legacy-staging per next session.
4. **SÌ** surface all'utente via ask_user con 3 halt options (defer entire chore / continue cascade / feature-branch emphasis migration only).

## Riferimenti

- AGENTS.md §`Mantenere baseline verde: 11/11 gate su ogni commit su main` (la baseline-verde è prerequisito non-negoziabile di push).
- AGENTS.md §`Disciplina di aggiornamento dei canonici` (commit-time only canonical churn).
- AGENTS.md §GATE-MNT-01 (fail-on-dirty: clean tree required pre-push).
- AGENTS.md v0.1 Cat-3 freeze-compliant (minimal-surface apply per ogni fix individuale, non bundle).
- TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT (4-commit cascade precedent).
- TICKET-TEXT-PRESET-SELECTORS-BRACE.md (sister-ticket Level 1).
- TICKET-RENDER-GRAPH-CACHEKEY-INCOMPLETE-TYPE.md (sister-ticket Level 2).
