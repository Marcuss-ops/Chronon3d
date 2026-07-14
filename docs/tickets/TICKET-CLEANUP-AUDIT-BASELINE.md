# TICKET-CLEANUP-AUDIT-BASELINE — Preflight cleanup-candidate baseline (read-only)

## Stato: OPEN (audit only, no chore commit)

## Scopo

Read-only preflight audit su `main` per identificare categorie di candidate al
cleanup successivo. **NO commit, NO push** — questa tabella è solo un
riferimento per future chore di pulizia. I counts sono macchina-verificati
tramite ripgrep su `main@6f96655a4d704943269b4e7c8ee0b59b8e440664`.

## Comando audit (reproducibile)

```bash
# Categoria 1: TextSpec boundary
rg -n '\bTextSpec\b' content apps examples tests/install_consumer

# Categoria 2: .key() canonical-anchor calls
rg -n '\.key\s*\(' src content apps examples tests

# Categoria 3: registry.add(...) factories
rg -n 'registry\.add\s*\(\s*"[^"]+"\s*,' content src apps examples tests

# Categoria 4: retarget_output (legacy)
rg -n 'retarget_output' src include tests

# Categoria 5: process_wide_ (broad-scope singletons)
rg -n 'process_wide_' src include tests

# Categoria 6: Camera subsystems
rg -n 'CameraRig|RigBakeDensity|CameraShotProfile|AnimatedCamera2_5D' \
    src content apps examples tests

# Categoria 7: persistent (broad, with CFB4 backing-store filter)
rg -n 'persistent|frame_invariant_persistent|static_persistent' \
    src/render_graph/executor tests
rg -n 'backing store|CFB4' src/render_graph/executor tests
```

## Counts (machine-verified)

| # | Categoria                                       | Count | Note                                    |
|---|-------------------------------------------------|-------|-----------------------------------------|
| 1 | `TextSpec` boundary                             | **0** | OK: nessun match         — migration forward-pointed via `TICKET-MOTIONTIMELINE-MIGRATION` / `TICKET-TEXT-SPEC-FORWARDER-REMOVAL` |
| 2 | `.key()` calls (motion-timeline anchor)         | **1008** | candidate non-chronon (legacy `AnimationTrack<T>`) — vasto bacino candidate per la sub-task 660-site `.key()` forward-point già documentata |
| 3 | `registry.add("…", lambda)` factories           | **204** | non tutti legacy; il subset `content/showcases/important-words/*` (5+ factory) è V1 canonical; verificare caso per caso |
| 4 | `retarget_output`                               | **1**   | single occurrence in `include/chronon3d/internal/render_graph/render_graph.hpp:131` (declaration, near-candidate per sunset) |
| 5 | `process_wide_`                                 | **8**   | la maggior parte sono reference in commenti di deprecation / retirement (Fase B2 DONE → globals REMOVED) |
| 6 | Camera subsystems (`CameraRig`/`RigBakeDensity`/`CameraShotProfile`/`AnimatedCamera2_5D`) | **278** | `CameraRig` SOLO (276/278) — gli altri 3 simboli sono ~zero o già sostituiti |
| 7 | `persistent` (broad)                            | **15**  | `frame_invariant_persistent`/`static_persistent` token vs `backing store`/`CFB4` = 2 legit filter (CFB4 codec) |
| 7 | CFB4 backing store (LEGENITTIMO)                | **2**   | non-candidate al cleanup (codec/store surface) |
| 7 | `persistent` NET non-backing-store              | **13**  | candidate set residuo dopo filtro |

Totale: **1510** raw matches prima filtro, **1495** dopo filtro cat 7 (CFB4 legit).

## Sample paths per categoria (5-line head, machine-verified)

### Categoria 2 — `.key()` calls
```
tests/visual/camera_truth/camera_truth_orbit.cpp:38:    orbit.yaw.key(Frame{0}, 0.0f).key(Frame{60}, 90.0f);
tests/visual/camera_truth/camera_truth_test.cpp:107:.key(Frame{0},  Vec3{0.0f,   0.0f, -1000.0f})
tests/visual/camera_truth/camera_truth_test.cpp:108:.key(Frame{30}, Vec3{300.0f, 0.0f, -1000.0f})
tests/visual/camera_truth/camera_truth_test.cpp:109:.key(Frame{60}, Vec3{-300.0f, 0.0f, -1000.0f});
tests/visual/camera/camera_visual_scenes.cpp:95:                .key(0, Vec3{0.0f, 0.0f, -1000.0f})
```

### Categoria 3 — `registry.add("…", lambda)`
```
content/backgrounds/grid_clean.cpp:24: registry.add("GridCleanBackground", [](const CompositionProps&) {
content/showcases/important-words/important_words_animations.cpp:164:registry.add("ImportantWordDirectorLight", […]);
content/showcases/important-words/important_words_animations.cpp:165:registry.add("ImportantWordActorWarm", […]);
content/showcases/important-words/important_words_animations.cpp:166:registry.add("ImportantWordWriterCool", […]);
content/showcases/important-words/important_words_animations.cpp:167:registry.add("ImportantWordTrio", [](…) { return important_word_trio(); });
```

### Categoria 4 — `retarget_output`
```
include/chronon3d/internal/render_graph/render_graph.hpp:131:
    void retarget_output(GraphNodeId id) {
```
*(single declaration — candidate per sunset/migration)*

### Categoria 5 — `process_wide_`
```
tests/text/test_text_font_determinism.cpp:54:///      set_process_wide_assets_root or other mutating operations).
tests/text/test_text_material.cpp:24:// `process_wide_assets_root()` channel; tests that need a custom root
src/runtime/render_engine.cpp:24:// `runtime::set_process_wide_assets_root`.  The retired orphan
src/runtime/render_engine.cpp:28:// truth, surfaced via `runtime::process_wide_assets_root()`).
src/runtime/render_runtime.cpp:50:// Fase B2 (DONE) — process_wide_assets_root globals REMOVED.
```
*(la maggior parte sono reference in commenti che documentano lo stato
"globals REMOVED" — Fase B2 — va valutato se mantenere doc-comment o
rimuoverli una volta che la Fase B2 è vista come stable)*

### Categoria 6 — Camera subsystems
```
src/scene/camera/camera_path_sampler.cpp:27:    const CameraRig& rig,
src/scene/camera/camera_path_sampler.cpp:174:    const CameraRig& rig,
src/scene/camera/camera_path_sampler.cpp:187:    const CameraRig& rig,
src/scene/camera/camera_v1/camera_descriptor_adapters.cpp:10:// CameraRig is mapped directly to the OrbitMotion source (canonical V1),
src/scene/camera/camera_v1/camera_descriptor_adapters.cpp:144:// Adapter 2: CameraRig (modern) → CameraDescriptor with OrbitMotion source
```

### Categoria 7 — `persistent`
```
src/render_graph/executor/node_executor.hpp:23://   - Dead branch removal at the persistent-cache call site INSIDE run_node.
src/render_graph/executor/node_executor.hpp:25://     `chore(runner): remove dead persistent-cache branch` commit (point 3
tests/assets/test_asset_resolver.cpp:7:// clamp-escape / on-disk-missing).  Uses a persistent temp directory
src/render_graph/executor/node_runner.cpp:10:#include <chronon3d/cache/persistent_framebuffer_store.hpp>
tests/cache/test_persistent_framebuffer_store.cpp:2:// test_persistent_framebuffer_store.cpp — CFB4 codec + store tests.
```

## Filtri noti (citati dall'audit)

### Categoria 7 — `backing store` / `CFB4` (legit non-candidate)

I 2 match `backing store|CFB4` identificano il codec/store surface canonico
**CFB4** (`tests/cache/test_persistent_framebuffer_store.cpp` + doc-comment).
CFB4 backing store è infra V1 e NON è un candidate al cleanup — è filter
positivo (categoria 7 NET = 13, non 15).

## Interpretazione per categoria

| # | Categoria       | Forward-point candidato |
|---|-----------------|--------------------------|
| 1 | TextSpec        | già DONE — nessun match residuo = OK |
| 2 | `.key()`        | CANDIDATE PRIORITARIO → `TICKET-MOTIONTIMELINE-MIGRATION` (660-site forward-point già documentato) + `TICKET-FLOAT-IDLE-MOTIONTIMELINE-MIGRATION-DEFER` (1 sin-wave site preservato) |
| 3 | `registry.add`  | NON prioritario: 204 site, la stragrande maggioranza sono V1 canonical (es. `important_words_*` factory cluster). Valutare subset-by-subset (es. legacy stubs). |
| 4 | `retarget_output` | CANDIDATE secondario: 1 sola declaration, sunset per render graph V2 |
| 5 | `process_wide_` | No-op (Fase B2 DONE — i 8 match sono commenti che documentano lo stato di rimozione) |
| 6 | Camera          | NON prioritario: `CameraRig` è V1 canonical (per `camera_path_sampler.cpp`/`camera_descriptor_adapters.cpp`). `RigBakeDensity`/`CameraShotProfile`/`AnimatedCamera2_5D` = ~0/278 → già sunset (forward-point `TICKET-CAMERA-FEATURE-MATRIX`) |
| 7 | `persistent`    | 13 NET candidate dopo filtro CFB4 — valutare subset-by-subset (dead-branch markers + on-disk persistent dirs) |

## Honest-limitation disclosures

- **Audit read-only**: nessuna modifica a `src/`, `include/`, `tests/`, `content/`, `apps/`, `examples/`.
- **Filtraggio cat 7**: sottrazione manuale `15 − 2 CFB4 legit = 13 NET`. Se nuovi `backing-store` matcher emergono dopo future FeatureWindow, ricalcolare.
- **Counts frozen al commit `6f96655a4d704943269b4e7c8ee0b59b8e440664`**: ogni post-FF pull potrebbe modificarli — ri-eseguire il comando audit per snapshot futuri.
- **Sample 5-line per categoria** è materiale di partenza, NON sostituisce full scan. Per categoria 2 (1008 match) e 6 (278 match), pianificare full scan out-of-band prima di qualsiasi migration decision.

## Forward-points (decision pre-condizionale)

| # | Future chore                | Pre-condizione                                |
|---|------------------------------|-----------------------------------------------|
| 2 | `.key()` → `AnimationTrack<T>` migration | full scan + 660-site sweep (sub-tasks come `TICKET-MOTIONTIMELINE-MIGRATION`) |
| 3 | `registry.add` legacy sweep  | subset-by-subset decision (es. legacy stubs vs canonical V1) |
| 4 | `retarget_output` sunset     | ADR (graph V2 semantic equivalence) oppure declaration permanence se dipendenza esterna |
| 7 | `persistent` candidate sweep | 13 NET site-by-site audit, con esclusione CFB4 |

## Comando riproducibile per ri-snapshot

```bash
# Dopo ogni FF-pull, ri-eseguire e confrontare:
for spec in \
  "rg -n '\bTextSpec\b' content apps examples tests/install_consumer" \
  "rg -n '\.key\s*\(' src content apps examples tests" \
  "rg -n 'registry\.add\s*\(\s*\"[^\"]+\"\s*,' content src apps examples tests" \
  "rg -n 'retarget_output' src include tests" \
  "rg -n 'process_wide_' src include tests" \
  "rg -n 'CameraRig|RigBakeDensity|CameraShotProfile|AnimatedCamera2_5D' src content apps examples tests" \
  "rg -n 'persistent|frame_invariant_persistent|static_persistent' src/render_graph/executor tests" \
  "rg -n 'backing store|CFB4' src/render_graph/executor tests"
do
  printf '%5d  %s\n' "$(eval "$spec" 2>/dev/null | wc -l)" "$spec"
done
```
