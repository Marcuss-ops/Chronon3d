# Camera Feature Matrix — Chronon3D

> **Snapshot funzionale camera analizzato:** `main@37c03c11` (C9a, runtime certifier), 2026-07-04.
>
> **Ultima baseline macchina-verificata:** `main@446a60e2` (baseline canonica; HEAD corrente non è baselined).
>
> **HEAD ricontrollato:** `main@37c03c11` (C9a, runtime certifier), 2026-07-04.
>
> Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Prova operativa: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).
> Piano canonico: [`camera-plan/`](camera-plan/).
>
> Una feature presente nel codice ma con test non eseguibili è 🟡, non ✅.

## Legenda

- ✅ **Verificato** — implementato e coperto da prova eseguita osservata.
- 🟡 **Implementato/parziale** — codice presente, ma limiti, migrazione o test aperti.
- 🔴 **Bloccante** — impedisce la chiusura della Camera Production V1.
- 🔵 **Pianificato** — design presente, implementazione non completata.
- ⚪ **Legacy** — mantenuto soltanto per compatibilità/migrazione.

## Valutazione complessiva

| Obiettivo | Stato | Nota |
|---|---|---|
| Camera Production V1 per motion graphics 2.5D | PARTIAL | Projection contract closed; golden test runtime PASS. Orbit, DOF, motion blur, e near-plane clipping verificati con test AE parity (89+ test PASS, 1 sentinel FAIL atteso). Framing, constraint avanzati, OrientAlongPath completo e legacy migration ancora aperti. |
| Parità camera molto ampia con After Effects | PARTIAL | Orbit, DOF animato, motion blur deterministico e near-plane clipping verificati (89+ test). OrientAlongPath completo, constraint solver e framing avanzato non ancora verificati. |

Per governance (vedi `docs/DOCUMENTATION_GOVERNANCE.md` §Pattern vietati) le
stime percentuali manuali di completezza non sono ammesse; lo stato si esprime
esclusivamente con `PASS / PARTIAL / NOT RUN / BLOCKED / PLANNED`.

## Stato della verifica corrente

L’ultima baseline documentata su `446a60e2` ha prodotto tre controlli verdi e un
target build rosso. La build si arresta attualmente su TICKET-039 nel confine
`RenderEngine`/`SoftwareRenderer`; TICKET-038 resta il blocker successivo noto.
Di conseguenza, i target camera non sono ancora raggiunti dalla baseline completa.

TICKET-029 resta un blocker specifico della compilazione/link dei test camera,
ma non deve essere descritto come l’unico o il primo blocker globale del
repository.

## 1. Architettura canonica

| Feature | Stato | Evidenza / limite |
|---|---|---|
| `Camera2_5D` snapshot runtime | ✅ | Tipo runtime usato da projection e renderer. Non deve essere l’authoring primario futuro. |
| `CameraDescriptor` authoring | 🟡 | Presente con source/orientation/modifier/constraint variant; migrazione composizioni non completa. |
| `compile_camera()` | 🟡 | Produce `CameraProgram`; la certificazione dei regression test è bloccata dalla catena TICKET-039/TICKET-038 e poi da TICKET-029. |
| `CameraProgram` immutabile | 🟡 | Entry point compilato presente e metadata dependency disponibili. |
| `CameraSession` per render job | 🟡 | Stato separato e checkpoint/pre-roll presenti; integrazione e prove complete da chiudere. |
| Fingerprint descriptor | 🟡 | Hash deterministico implementato; includere nei gate di regressione. |
| Default descriptor in `Composition` | 🟡 | Integrazione presente; consumer e full-validation da verificare. |
| Unico percorso authoring | 🔴 | Coesistono ancora camera diretta, `AnimatedCamera2_5D`, rig moderni/legacy e compiled path. |

Percorso obbligatorio per nuove feature:

```text
CameraDescriptor
    → compile_camera()
    → CameraProgram
    → evaluate(CameraEvalContext, CameraSession)
    → Camera2_5D
    → projection contract
    → renderer
```

## 2. Projection e ottica

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Zoom projection | ✅ | Contratto esistente e test storici. |
| Vertical FOV projection | 🟡 | Supportata; fix recente evita che PoseTracks la sovrascriva con Zoom. Regression run da certificare. |
| Physical Lens projection | 🟡 | Variant e `LensModel` presenti; percorso e parity completa da verificare. |
| Focal length / sensor size / f-stop | 🟡 | Modello presente e animabile in parti del sistema; ownership da rendere unica. |
| Focal X/Y separati | ✅ | TICKET-035 chiuso (C1–C2 @ `eb1ce8e5`): `FocalPx` POD, `focal_xy_from_camera` consume via `LensModel::focal_xy_pixels`. Parità contract ↔ resolver ↔ framing verificata. |
| GateFit Fill | 🟡 | Presente nel lens/projection contract. |
| GateFit Overscan/Fit | ✅ | C1 + C5 + C6 @ `eb1ce8e5`: `effective_viewport()` espone offset pillarbox/letterbox; sentinel regression (`tests/scene/camera/test_camera_projection_contract.cpp` "Sentinel: effective_viewport fits within requested viewport in every GateFit mode") blocca qualunque regressione. |
| GateFit Stretch | ✅ | C5 + C6 + C7 @ `eb1ce8e5`: focal X/Y indipendenti via `LensModel::focal_xy_pixels`; golden test Mode 4 (`tests/scene/camera/golden_projection_test.cpp`) locka i numeri. |
| Pixel aspect | 🟡 | Contratto presente e propagato in `EvaluatedProjection::pixel_aspect`; non coperto da golden test dedicato. |
| Anamorphic squeeze | ✅ | TICKET-035 chiuso (C7 @ `eb1ce8e5`): `anamorphic_squeeze` letto da `LensModel`, applicato SOLO a focal_x. Golden test Mode 6 locka i numeri (ratio `focal_x / focal_y = 3.011` × 1.506 base × 2.0 squeeze). |
| Near/far plane | 🟡 | Parametri/contratto parziali. |
| Clipping point | 🟡 | Fondazioni presenti. |
| Clipping segment/quad/polygon | ✅ | FASE 3I: 10 test (quad + triangle + pentagon) verificano clipping, behind-camera invisibility, no NaN, no exploded triangles. `chronon3d_scene_tests` 10/10 PASS. |

## 3. Source e movimento

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Static camera source | 🟡 | Presente nel compiled path. |
| Pose Tracks | 🟡 | Posizione, rotazione, target, zoom/FOV e DOF channels presenti. |
| Orbit Motion | ✅ | FASE 3F: 7 test compilati PASS (basis forward per yaw, dolly camera-local, roll rotation, parent propagato). `chronon3d_scene_tests` 7/7 PASS. |
| Trajectory Motion | 🟡 | Tipo e trajectory path presenti; base-state preservation e test completi ancora aperti. |
| Arc-length LUT | ✅ | Implementazione e regression test documentati. |
| Sub-frame `SampleTime` | ✅ | Animated values, camera, composition e temporal samples usano il contratto sub-frame. |
| Idle oscillation | 🟡 | Modifier assoluto nel tempo presente. |
| Handheld noise | 🟡 | Modifier deterministico absolute-time presente. |
| Dolly zoom | 🟡 | Helper/preset esistenti; regression e percorso canonico da chiudere. |
| Product orbit / fly-through / dashboard tour | 🔵 | Preset prodotto, non primitive core; introdurre solo nel catalogo canonico. |

## 4. Orientamento

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Fixed orientation | 🟡 | Variant presente. |
| Look-at point | 🟡 | Presente; fix recente evita doppia applicazione. Regression run da certificare. |
| Look-at layer | 🟡 | Variant e context dependency presenti. |
| Roll/local offset | 🟡 | Presente in source/rig; ordine canonico da proteggere con parity test. |
| Parent orientation | 🟡 | Fondazioni esistenti; integrazione unica da chiudere. |
| `OrientAlongPath` type | 🟡 | Variant dichiarata. |
| `OrientAlongPath` completo | 🔴 | TICKET-023: tangent provider, fallback, look-ahead, keep-horizon e banking non chiusi. |
| Quaternion continuity / shortest arc | 🟡 | Parti presenti; evitare conversioni Euler come rappresentazione primaria. |

## 5. Constraint e framing

| Feature | Stato | Evidenza / limite |
|---|---|---|
| LookAt constraint | 🟡 | Spec tipizzata presente. |
| KeepHorizon constraint | 🟡 | Spec tipizzata presente. |
| DampedFollow constraint | 🟡 | Stateful; richiede session/pre-roll corretti. |
| Distance constraint | 🟡 | Spec tipizzata presente. |
| RotationLimit constraint | 🟡 | Spec tipizzata presente. |
| Camera checkpoint/pre-roll | 🟡 | Implementato per random access; test completi da certificare. |
| Basic fit/framing | 🟡 | Helper/solver di base presenti. |
| Bounds reali dei layer | 🔵 | Non affidarsi a tabelle `layer_sizes`. |
| Multi-target framing | 🔵 | Da implementare nel solver canonico. |
| Rule-of-thirds | 🔵 | Da implementare nel solver canonico. |
| Dead-zone/hysteresis/look-ahead | 🔵 | Da implementare senza matematica prospettica duplicata. |
| DollyZoom solver | 🔵 | Deve usare lo stesso projection contract. |

## 6. Motion blur e depth of field

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Temporal multi-sample motion blur | ✅ | FASE 3H: 12 test PR8 (disabled/static/narrow/deterministic/moving) + 1 PR1-Torture deterministico across consecutive runs. 13/13 PASS in `chronon3d_scene_tests`. |
| `MotionBlurMode` source of truth | 🟡 | Bool legacy rimosso; regression run da certificare. |
| Shutter angle/phase/pattern/filter | 🟡 | Contratti presenti in più parti; verificare end-to-end e cache keys. |
| Depth buffer / per-pixel DOF foundation | 🟡 | Backend support presente. |
| Focus distance / aperture | ✅ | FASE 3G: animated focus distance (PoseTracksSource + keyframe interpolation 5-frame), DOF transfer via trajectory, + 10 test DOF in `chronon3d_camera_tests`. 13/13 PASS. |
| Circle of Confusion fisico | 🔵 | Non ancora chiuso come modello canonico. |
| Near/far blur separati | 🔵 | Da implementare. |
| Iris blades/rotation/roundness | 🔵 | Modello/rendering non produttivo. |
| Anamorphic bokeh / focus breathing | 🔵 | Effetti avanzati post-V1. |

## 7. Shot timeline e transizioni

| Feature | Stato | Evidenza / limite |
|---|---|---|
| `ShotTimeline` | 🟡 | Sequenza di programmi camera presente. |
| Per-shot sessions | 🟡 | `ShotTimelineSession` presente. |
| Cut | 🟡 | Implementazione/factory presente. |
| Smooth Blend | 🟡 | Implementazione/factory presente. |
| Push | 🟡 | Implementazione/factory presente. |
| Whip Pan | 🟡 | Implementazione/factory presente. |
| Focus Handoff | 🟡 | Implementazione/factory presente. |
| Transition catalog | 🟡 | Catalogo DI, non singleton, presente. |
| Diagnostica completa | 🔴 | TICKET-027: non tutta la diagnostica viene propagata. |
| Random-access determinism | 🟡 | Checkpoint/pre-roll presente; integrazione timeline da certificare. |

## 8. Diagnostics e tool

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Camera debug overlay | 🟡 | Safe area, target, bounds/path e viste diagnostiche presenti. |
| Camera shot validator | 🟡 | Validazioni di base presenti; soglie/options da canonicalizzare. |
| CLI path report | 🔵 | Non esposta come comando prodotto stabile. |
| CLI camera validate | 🔵 | Pianificata. |
| CLI debug video | 🔵 | Pianificata. |
| JSON report stabile | 🔵 | Schema/versionamento da definire. |
| Golden camera suite | ✅ | Suite committed in `tests/scene/camera/golden_projection_test.cpp`: 1 TEST_CASE × 6 SUBCASEs (Zoom, FOV 50°, PhysicalLens ARRI, GateFit::Stretch, GateFit::Overscan, Anamorphic 2×) lockati con tolleranza 1e-3, hash-free per stabilità cross-host. Certificazione runtime PASS (71/71 assertions) su C9a (`37c03c11`). Test eseguito in `build/tests/chronon3d_scene_tests` post-C9a (build clean, unity-build escluso per `timed_text_document.cpp` + `boundary_resolver/text_unit_map.cpp` per ODR TU-locali pre-esistenti). |

## 8.1 Cross-link — Text SafeArea placements (M1.8 §3B)

SafeArea-aware placement è una concern di **layout/compositing**, non di camera. La camera non possiede il concetto di safe-area; questo è delegato al text pipeline canonico:

- [`include/chronon3d/text/text_placement.hpp`](../include/chronon3d/text/text_placement.hpp) — `TextPlacementKind` enum (16 varianti includendo `SafeAreaTop`/`Bottom`/`Left`/`Right`/`Center`) + `SafeAreaPreset` (4 aspect-ratio presets) + user-facing `SafeAreaEnum` (5 valori).
- [`include/chronon3d/text/text_placement_resolver.hpp`](../include/chronon3d/text/text_placement_resolver.hpp) — `resolve_safe_area(SafeAreaEnum) → TextPlacement{SafeArea*}` mapping canonico + `CanvasInfo::with_safe_area(...)` factory per aspect-ratio-aware safe-area margins.
- [`src/text/text_placement_resolver.cpp`](../src/text/text_placement_resolver.cpp) — single switch su `SafeAreaEnum` (5 casi) + `resolve_placement_origin` switch (16 casi); nessuna tabella di mapping parallela.
- [`tests/text/test_safe_area_placement.cpp`](../tests/text/test_safe_area_placement.cpp) — 8 sub-cases (4 placements × 2 canvases 1920×1080 + 1080×1920) + 5 SafeAreaEnum sweep + cross-link equivalence path A↔path B.

Pin-point semantics (16 placements total, safe-area family 5/16):

| Variant | Pin point (1920×1080) | Pin point (1080×1920) |
|---|---|---|
| `SafeAreaTop` | (960, 54) | (540, 96) |
| `SafeAreaBottom` | (960, 1026) | (540, 1824) |
| `SafeAreaLeft` | (96, 540) | (54, 960) |
| `SafeAreaRight` | (1824, 540) | (1026, 960) |
| `SafeAreaCenter` | (960, 540) | (540, 960) |

Safe-area margins derivation (per `SafeAreaPreset::Landscape16x9` / `Portrait9x16` etc., 5% di default): `top=0.05×H`, `bottom=0.05×H`, `left=0.05×W`, `right=0.05×W`. Risoluzione canonica: `CanvasInfo::with_safe_area(width, height, preset)`.

Anti-duplicazione honour: un solo switch `SafeAreaEnum → TextPlacement` nel resolver; nessuna tabella parallela. Le 5/16 varianti safe-area sono parte del `TextPlacementKind` enum (non un secondo sistema).

---

## 9. Catena di blocker per la certificazione camera

### TICKET-039 — blocker globale corrente

La baseline si arresta nel percorso `RenderEngine`/`SoftwareRenderer` prima di
raggiungere la certificazione completa dei target camera. Correggere il consumer
verso l’accessor canonico `render_settings()` e rieseguire il target scopritore.

### TICKET-038 — blocker globale successivo noto

Il visual test testuale contiene un rot di lambda capture/deduzione che potrebbe
manifestarsi subito dopo TICKET-039. Deve essere chiuso prima di dichiarare la
catena di build sufficientemente sana per una nuova baseline.

### TICKET-029 — blocker specifico camera

`camera_program_compiler.cpp` presenta una rottura di type visibility che blocca
link ed esecuzione dei test scene/camera. Finché è aperto, i fix camera recenti
con test compilati isolatamente restano 🟡.

Dopo la chiusura della catena, rieseguire almeno:

- projection variant preservation;
- single orientation application;
- orbit local basis;
- motion blur mode;
- checkpoint/pre-roll;
- projection focal X/Y e gate fit;
- composition default descriptor.

## 10. Definition of Done — Camera Production V1

La milestone è chiusa quando:

1. le nuove composizioni usano `CameraDescriptor` o `CameraProgram`;
2. `CameraSession` appartiene al render job;
3. non avvengono compile/catalog lookup per frame;
4. projection e orientation hanno un solo contratto;
5. `OrientAlongPath` è realmente operativo;
6. test scene/camera collegano ed eseguono in CI;
7. parity e golden sono bloccanti;
8. adapter legacy hanno regression parity e data/fase di rimozione;
9. il consumer SDK esterno usa una camera compilata;
10. nessun secondo registry, evaluator o projection contract è stato introdotto.
