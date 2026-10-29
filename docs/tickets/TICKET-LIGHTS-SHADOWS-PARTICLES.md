# TICKET-LIGHTS-SHADOWS-PARTICLES — luci reali, ombre portate, particelle veloci

Stato: **PARTIAL — OPEN** (scope scelto dall'utente il 2026-10-08: luci reali con ombre
portate, e istanziamento GPU per le particelle).

### Gate T05 — verifica C3D 2026-10-22

- Il RenderPlan A/B C3D software produce pixel validi e modifica i pixel del
  receiver quando i caster sono attivi. Evidenza e metriche:
  [`T05 C3D shadow investigation`](../evidence/lights-shadows/T05-c3d-production-20261022/).
- La qualità non è certificata: nella scena di verifica le ombre sul pavimento
  hanno contrasto debole e il confronto con gap caster/receiver ridotto non lo
  migliora. Non è una composizione di produzione approvata.
- Il render Vulkan `require_gpu_native` fallisce nel nodo `Shadow` con
  `UnsupportedCapability: shadow requires a native GPU pass or CPU pixel backing`.
  `ShadowNode` resta una reference CPU; il percorso nativo C3D non è integrato.
- T05 rimane **PARTIAL**. Per chiuderlo servono un native shadow pass C3D, un
  A/B di qualità su uno shot di produzione e la revisione visiva della composizione.

### Gate T05 — pass Vulkan nativo 2026-10-26

- `ShadowNode` ora ha un ramo `require_native_gpu` che proietta l'alpha del
  caster, applica la tinta e le passate blur e compone le ombre contact/ambient
  usando superfici native. Gli input GPU-only restano GPU-authoritative; i
  caster CPU-authoritative vengono promossi a una superficie temporanea.
- Il test di regressione Vulkan verifica proiezione, tinta/alpha, autorità GPU
  e ripetibilità byte-identica. `chronon3d_backend_registry_tests`: **1/1
  target passato** (38.79 s); il test aggiunto contiene 15 asserzioni. Il test
  è stato eseguito nel WSL corrente: `vulkaninfo` seleziona `llvmpipe` CPU, per
  cui verifica le operazioni Vulkan native ma non la RTX fisica.
- Il strict render CLI T05 fallisce prima dell'export con
  `required_gpu_capability_unavailable`. In WSL non è esposto un device Vulkan
  NVIDIA. Il render software di controllo termina, ma usa il ramo CPU di
  `ShadowNode` e non costituisce prova visiva del pass nativo.
- NVENC funziona con il FFmpeg di sistema; la build CLI collegata non rileva
  l'encoder NVENC. Risolto il device Vulkan, servirà anche un export di review
  che non falsi il gate strict o una build FFmpeg collegata con NVENC.
- Stato T05: **PARTIAL**. Il percorso nativo e il test backend sono implementati;
  la verifica sul device fisico e il gate di produzione restano aperti.
  Dettagli: [`evidenza pass Vulkan`](../evidence/lights-shadows/T05-native-vulkan-20261026/README.md).

### Gate T05 — tentativo RTX Windows 2026-10-26

- Preparata una build CLI Windows persistente con Clang, Vulkan, CUDA interop e
  FFmpeg/NVENC nativo. Il binario e la cache CMake sono in
  `Chronon3d-shadow-windows/build/win-shadow-clang`; i passaggi per riusarli
  senza reinstallare dipendenze sono in
  [`setup RTX Windows`](../evidence/lights-shadows/T05-native-vulkan-20261026/WINDOWS-RTX4060TI-SETUP.md).
- Sulla RTX 4060 Ti, Vulkan, CUDA/Vulkan interop e NVENC si inizializzano; il
  piano strict-native viene risolto. Il render T05 fallisce poi al nodo
  `Lighting`: input privo sia di superficie nativa valida sia di pixel CPU.
  NVENC riceve zero frame, quindi non esiste un'immagine utile per valutare le
  ombre. Log: `build/win-shadow-clang/t05-run-native.log` nel worktree Windows.
- Stato T05 resta **PARTIAL**. Il pass nativo ha la regressione Vulkan coperta,
  ma la pipeline strict-native Windows non ha ancora prodotto il frame T05 e
  manca la revisione visiva A/B di qualità.

### Requisito ambiente per certificazione hardware

Per chiudere il gate T05 serve eseguire la regressione e il render del RenderPlan
su un device Vulkan NVIDIA reale, poi confrontare output senza/con ombre. Questo
host Windows vede la RTX 4060 Ti con Vulkan, ma il target C3D Windows non
compila ancora: il backend include `unistd.h` non portabile e il validator MSVC
fallisce sugli identificatori `near`/`far` e sugli schema JSON generati troppo
lunghi. Il WSL locale usa solo `llvmpipe` per Vulkan. Finché uno di questi
blocchi non è risolto, T05 resta **PARTIAL**.

## TODO — stato aggiornato 2026-10-29

- [x] Pass `ShadowNode` Vulkan strict-native implementato e un frame esportato
  su RTX A4000/NVENC (evidenza sotto).
- [ ] **A/B strict-native stabile + revisione qualità** — logica: l'accettazione senza probe del 29 Oct è completa 5/5 off e 5/5 on, ma il rerun Release guarded successivo è sceso a 2/5 per entrambi (sync-validation: 2/5 off, 3/5 on). Gli snapshot sondati hanno output Transform/Lighting talvolta zero; la causa non è provata. Stabilizzare l'esecuzione, ripetere l'A/B e revisionare l'ombra su shot di produzione. Dove: native surface path e backing cropped reuse.
- [x] Istanziamento GPU per particelle circolari, bokeh e sprite nel percorso
  Vulkan strict-native: un `SourceNode` per emettitore invia le istanze campionate
  a un dispatch `LayerBatch`; gli sprite riusano la superficie asset preparata.
  Il ramo CPU conserva la materializzazione esistente e il ramo strict fallisce
  esplicitamente se il backend non offre il batching.
- [x] **Regressione Vulkan locale + misura C2** — suite backend registry completa sul tree corrente PASS (1/1 CTest, 223.09 s; include il pass nativo e la regressione upload→frame-batch 8 iterazioni/50 assert); C2 strict Vulkan/NVENC misurato a 1k/10k/100k. Evidenze in `docs/evidence/lights-shadows/T05-native-vulkan-20261029/`. Questi risultati locali non sostituiscono la certificazione sullo SHA pubblicato.
- [ ] **Regressione e certificazione same-SHA dopo review/commit.**

Per localizzare il backing fisico quando l'output è zero, il batch conclusivo
può abilitare `CHRONON3D_T05_SURFACE_ID_DIAG=1`: affine Transform e Lighting
registrano handle logico, identità `VkImage`, pass, piano sync e stato
initialized. Correlare con `[native-content-diag]` e i normali run senza probe.
La sonda aiuta a distinguere riuso/alias del backing, ma non prova la stabilità.

### Aggiornamento 2026-10-28 — prova RTX A4000

- Il host Linux espone una NVIDIA RTX A4000, Vulkan NVIDIA e NVENC. La CLI
  strict-native ha completato il piano C3D di un frame (`required_gpu_native_full_graph`):
  quattro nodi `Shadow` risultano GPU-only e NVENC accetta un frame/produce un
  pacchetto H.264. Il PNG decodificato è in
  [`evidenza frame strict-native`](../evidence/lights-shadows/T05-native-vulkan-20261028/README.md).
- Questa prova sostituisce il precedente blocco ambientale “WSL senza GPU” per
  questo host, ma non certifica la qualità. La revisione del frame non mostra
  un'ombra portata chiaramente visibile.
- Dopo la correzione del riuso della backing cropped, entrambi i render
  strict-native on/off terminano e NVENC muxa un frame. Il controllo off
  decodifica però in un'immagine completamente nera (media RGB 0), mentre il
  frame on mostra il piano e i due caster: la coppia non è un A/B valido. I
  log del controllo riportano sostituzioni di superfici 1920×1080/218×287 e
  1920×1080/336×395, senza errore Vulkan; va ancora corretta la causa del
  contenuto nero e ripetuto il confronto.
- Il test backend Vulkan isolato resta verde per proiezione/blur; il percorso
  affine `Screen` e i chiamanti della superficie nativa sono stati ricompilati
  in modo mirato, senza build completa. La suite completa e la certificazione
  same-SHA non sono state eseguite.
- L'instanziamento GPU è implementato nel tree corrente; la regressione runtime,
  la parità visiva e la certificazione same-SHA sono ancora da eseguire. T05
  resta **PARTIAL — OPEN**.

### Istanziamento particelle strict-native — implementazione 2026-10-28

- Ogni layer particelle conserva i nodi CPU di riferimento e porta anche gli
  stati campionati dell'emettitore. Il graph builder, solo con
  `require_native_gpu`, li instrada a un singolo source per emettitore.
- Il kernel Vulkan `LayerBatch` rasterizza dischi/bokeh proceduralmente o
  campiona uno sprite condiviso con tinta, scala e rotazione per istanza. Il
  backend spezza i buffer in gruppi da 512 per rispettare il limite di update
  Vulkan; l'ordine painter resta stabile fra i gruppi.
- La regressione verifica compositing e conteggio delle istanze del kernel.
  Shader/SPIR-V/ABI, backend, graph builder, materializer e unità di test sono
  stati compilati; la regressione RTX A4000 ora passa: 2 casi / 22 asserzioni
  per batching di dischi e sampling sprite con affine/tinta/opacità. Parità
  visiva, prova hardware same-SHA e misure C2 restano aperte; la feature è
  implementata, ma T05 resta **PARTIAL — OPEN**.

### Aggiornamento 2026-10-28 — regression clone e indice del piano

- Build `chronon3d_cli`, `chronon3d_core_tests` e
  `chronon3d_backend_registry_tests` riuscita con Ninja `-j12` dopo
  l'introduzione della copia ausiliaria che non consuma l'indice del
  `CommandPlan`.
- La regressione Lighting→clone nello stesso batch passa su RTX A4000 (1 caso /
  34 asserzioni), con snapshot `JobPersistent` prima del submit; il clone GPU
  non pianificato da superficie caricata passa a 3 dimensioni (1 caso / 36
  asserzioni); il clone con slot pianificato passa (1 caso / 7 asserzioni).
- La suite particelle Vulkan passa (2 casi / 22 asserzioni). Non sono ancora
  state rieseguite le scene T05 complete: il fix dell'indice è candidato e il
  controllo caster-off, la ripetibilità, la review qualità e C2 restano aperti
  fino ai rerun finali.

## Audit di partenza (2026-10-08)

### Avanzamento verificato (2026-10-10)

- I layer RenderPlan `point`, `spot` e `directional` vengono compilati in
  `PointLightSource` sul `LightContext` di scena; il nodo lighting li valuta
  per pixel. Il tipo `radial` conserva l'alone storico. Intensità e colore
  hanno tracce animate per frame.
- Il compiler materializza le ombre richieste come `DropShadow` CPU sul
  caster. Questa è una proiezione screen-space approssimata, non una shadow
  map né un sistema di receiver indipendenti. `casts_shadows` controlla il
  caster; `accepts_shadows` non deve sopprimere l'ombra propria del caster.
- Test già presenti in `test_prepared_render_plan.cpp` coprono il lowering
  delle tre sorgenti, altezza predefinita, animazione e ombre; i test renderer
  coprono i calcoli closed-form e i casi spot/point.
- Restano aperti: animazione di posizione/direzione della luce, ombre con
  receiver espliciti e parità GPU, più batching/istanziamento delle particelle.

### Implementazione locale (2026-10-11)

- Le opzioni `cast_shadow`, `shadow_radius` e `shadow_bias` delle luci point,
  spot e directional vengono propagate nel `LightContext` e incluse nel suo
  hash. Il compiler non aggiunge più `DropShadow` statici al layer caster.
- Il graph genera ombre per receiver che accettano ombre e caster che le
  proiettano. Le sorgenti point/spot rispettano range e cono spot; raggio e bias
  sono passati al `ShadowNode`.
- Questo percorso usa ancora `ShadowNode`, che richiede superfici CPU in strict
  GPU mode: la parità Vulkan e il fail-closed verificato sono ancora aperti.
- Le particelle ambiente condividono ora il limite 4096/64 MiB del ticket
  cinematico. Non è stato introdotto un draw instanziato: C1 per-draw, C2
  benchmark e C3 Vulkan restano aperti.

### Tranche animazione posizione/direzione — candidato locale

- Priorità confermata dall'utente: luci prima delle particelle.
- Le sorgenti point/spot/directional campionano `position` (vettore assoluto) e
  `position_x/y/z` (offset), riusando il sampler transform canonico; l'altezza
  implicita di una luce 2D resta il suo radius.
- `light_direction` è una track Vec3 nel registry e nello schema V3. Decoder e
  sampler rifiutano vettori non finiti/nulli, duplicati e uso su luci radial;
  un'interpolazione che attraversa il vettore nullo fallisce esplicitamente.
- Regressioni aggiunte per start/mid/end, accesso fuori ordine, fingerprint,
  arità/duplicati e vettore nullo interpolato. Build/test/CLI: **VERIFICATO con SUCCESSO**
  (`chronon3d_c_abi_tests`: 269/269 test cases passed, 6666 assertions passed;
  `chronon3d_effects_tests`: 275/275 passed; `tools/run_developer_gates.sh --slim`: 6/6 passed).
- Scope: materializzazione `CompiledComposition`/video compatibility. Il typed
  compiler non ha ancora `NodeKind::Light`: PNG/SDK typed richiedono una tranche
  di lowering/materializzazione separata; non dichiarare parità fra le route.
- Ombre receiver-owned e parità GPU rimangono aperte; il DropShadow screen-space
  esistente non cambia semantica in questa tranche.

### Luci (audit storico, precedente alla tranche del 2026-10-10)
- `LayerType::Light` (`render_plan.hpp`) disegna solo un alone radiale
  (`LightLayerPlan{radius, color, intensity}`). Il layer ha `accepts_lights(false)`
  e **non illumina nessun layer**.
- `LightContext` (`include/chronon3d/rendering/light_context.hpp`) è un modello
  2.5D globale (ambient + N·L + speculare + rim) applicato solo ai materiali
  `Material2_5D` con `accepts_lights`. Viene alimentato da `scene.light_context()`,
  non dal render plan.
- `ShadowSettings` (`rendering/shadow_settings.hpp`) descrive ombre di contatto,
  ambient e depth-aware; il nodo `ShadowNode` esiste, ma non è collegato a una
  sorgente luce.

### Particelle
- Campionatore stateless `sample_particles` (`scene/model/shape/particles.hpp`):
  emettitori Box/Ellipse/Edge/Path, vita, colore, gravità, turbolenza, sprite,
  rotazione, blend mode. Limite 1.000.000 particelle.
- `render_plan_compiler_scene.cpp` converte **ogni particella in un nodo**
  (`builder.circle` o `builder.image`). Con grandi quantità il costo è nel
  numero di nodi, non misurato.
- Test: `tests/scene/shapes/test_particles_sampling.cpp` (6 casi), più
  `tests/c_abi/*` e `tests/scene/motion/test_offline_motion_plan_adapter.cpp`.
- Nessun istanziamento GPU.

### Riferimento ChrononMotion3D
- `include/chrononmotion/motion/Lighting.hpp`: `LightKind` Ambient/Directional/
  Point/Spot; tracce animate per intensità, colore, posizione, rotazione,
  direzione, range, decay, angolo, penombra; `castsShadow`, bias, normal bias,
  raggio ombra.
- `RenderPlanEmission.hpp`: emette emettitori di particelle (condividono
  `ParticleEmitterParams` con Chronon3d) e luci per layer. Le luci globali
  (`lighting rigs`) sono rifiutate esplicitamente.
- `Lighting.hpp` è indipendente dal renderer: il consumatore è da scrivere.

## Criteri di accettazione

### A. Luci (render plan)
- A1. `LightLayerPlan` accetta `kind` ∈ {point, spot, directional}, `range`,
  `decay`, `angle`, `penumbra`, `direction`, `cast_shadow`, `shadow_radius`,
  `shadow_bias`. Default retrocompatibili: un layer `light` senza `kind` resta un
  alone radiale identico a oggi.
- A2. Decoder, validatore, fingerprint e schema JSON (`schemas/json/`) aggiornati;
  input invalidi (range ≤ 0, angle fuori [0, π], decay < 0) sono rifiutati con
  messaggio esplicito.
- A3. Un layer luce illumina i layer sottostanti con falloff: point ∝ 1/(1+d^decay)
  entro `range`; spot con cono e penombra; directional uniforme.
- A4. Tracce animate per posizione, intensità, colore, direzione (come in
  ChrononMotion3D) campionate per frame in modo deterministico.
- A5. Test per ogni tipo: valore di illuminazione in punti noti (closed form).

### B. Ombre portate
- B1. Un layer con `cast_shadow` proietta la propria silhouette (alpha) sui layer
  ricevitori nella direzione della luce (directional) o radialmente (point/spot).
- B2. Blur `shadow_radius`, bias per evitare auto-ombreggiatura, opacità.
- B3. Test: ombra di un rettangolo su sfondo bianco, posizione e dimensione attese
  entro tolleranza di 1 px; nessuna ombra se `cast_shadow` è false.

### C. Particelle
- C1. CPU: un solo nodo di disegno per emettitore (batch), output identico byte per
  byte alla versione per-nodo sullo stesso seed (test di parità).
- C2. Misura con `chrono` su 1k, 10k, 100k particelle, 1080p, 1 frame: tempi
  riportati nel ticket. Nessun claim di velocità senza questa misura.
- C3. Vulkan: disegno istanziato; parità con il percorso CPU entro tolleranza
  documentata; test che fallisce se il backend non è Vulkan (no fallback silenzioso).

### D. Riferimento ChrononMotion3D
- D1. Le tracce di luce e le particelle emesse da ChrononMotion3D
  (`tests/motion_authoring.cpp`) continuano a decodificare e a renderizzare.

## Verifica finale
- Target: `chronon3d_c_abi_tests`, `chronon3d_render_plan_*`, `chronon3d_render_graph_tests`,
  `chronon3d_cli_tests`, `chronon3d_particles` (nuovo se serve), più la suite
  ChrononMotion3D `motion_authoring`.
- Ogni claim con exit code e numero di casi.

### Audit statico aggiuntivo — 2026-10-29

Il route T05 osservato in CLI entra nel normale `begin_frame_batch()` (il trace
locale precedente ha mostrato `command_plan == nullptr`). Le operazioni di
superficie passano da `run_batched_surface_op()`, che acquisisce il submission
mutex durante l'intera registrazione; descriptor allocation, aggiornamento dei
binding e `frame_batch.pass_count` non vengono quindi eseguiti in parallelo da
questo percorso. Questo restringe l'ipotesi di race sul command buffer/indice,
ma non identifica la causa del contenuto zero ai nodi Transform. Ownership e
riuso delle immagini, lifetime dei framebuffer e stato del grafo restano da
isolare; T05 rimane **PARTIAL — OPEN**.

### Lifetime and affine-transform audit — 2026-10-29

Static review found no missing `Image::initialized` reset in the Vulkan image
materializer or transient-retirement path: rematerialization clears the flag,
and retiring an unleased transient clears it before slot reuse. This does not
prove the wider graph lifetime correct. Added the opt-in
`CHRONON3D_T05_TRANSFORM_DIAG=1` trace at the native affine Transform call; it
records source/destination handles, output extent and origin, dispatch origin,
local clip, source-coordinate mapping, projective denominator bits, opacity,
sampling mode and source-over mode. The backend emits the actual clamped
dispatch rectangle/groups and destination initialization state separately, so
the recorded dispatch is not inferred from node inputs. Use it in the final strict-native rerun to
compare empty and visible Transform outputs. It also marks the terminal
encode-surface case, which skips the transparent clear to preserve existing
contents; verify that its dispatch covers the intended output. The cause
remains open until that rerun identifies a concrete mismatch and the fix passes
the repeated caster on/off gate.

### Transform/content snapshot rerun — 2026-10-29

One strict Vulkan/NVENC `caster-off` render completed with the Transform and
surface-snapshot probes enabled. Dispatches had valid bounds and initialized
surfaces. Readback snapshots showed non-zero floor and hero Transform outputs
(means 0.433264 and 0.221241) followed by zero Lighting outputs; the secondary
caster remained visible through Lighting (mean 0.166338). The 1408×595 empty
Transform is fed by a transparent Clear node and is not by itself evidence of
lost authored content. This localizes one observed loss after Transform, near
the Lighting read/write or resource-lifetime boundary, but one probed frame
does not establish the root cause. Full per-node results are in
`docs/evidence/lights-shadows/T05-native-vulkan-20261028/README.md`; five
strict-native repeats without snapshots pass as recorded below. The root cause,
shadow quality and C2 remain open. T05 stays **PARTIAL — OPEN**.

### Repeated no-probe acceptance — 2026-10-29

Five strict Vulkan/NVENC reruns per mode, without diagnostic probes, all encoded
and decoded. Caster-off: 5/5 complete, identical decoded RGB hash, mean
53.180168/255. Caster-on: 5/5 complete, three decoded hashes, means
53.445176–53.529876/255. None was black or backdrop-only, improving the
observed complete-output rate from 3/5 to 5/5 in both modes versus the 28 Oct
rerun. The visual on/off pair differs by mean absolute channel value
0.470675/255, with 105,575 pixels (>3 code values) changed over the
subjects/floor area; the cast shadow is visible but subtle.

A separate `CHRONON3D_PATH_DIAG=1` render reported the same finite lighting
multiplier `(0.5790, 0.5303, 0.4549)` for floor and both casters. Clone logs
showed distinct source/destination surface handles for passthrough copies;
this reduces support for a bad multiplier or simple logical-handle alias, but
does not prove which physical resource was read by each Lighting dispatch.
The snapshot-only zero Lighting observations therefore remain unexplained and
the general lifetime-black cause is not claimed solved. Full logs, MP4s,
per-run hashes and pixel statistics are in
`docs/evidence/lights-shadows/T05-native-vulkan-20261029/`.

### C2 particle measurements — 2026-10-29

The reproducible 1080p/one-frame plans were rendered in strict Vulkan with
native NVENC and decoded for a pixel digest. End-to-end CLI wall time, including
startup and encoder close/mux, was 2.047 s at 1k, 2.607 s at 10k, and 7.998 s
at 100k particles. Decoded RGB means were 10.184, 11.812, and 24.978/255,
respectively. These are the first measurements for these fixtures, not a
before/after speedup claim; the wall time is not isolated GPU kernel time.
Commands, side-by-side output digests and logs are in
`docs/evidence/lights-shadows/T05-native-vulkan-20261029/c2-results.json`.
The separate particle instancing/parity acceptance remains tied to the broader
GPU and same-SHA gates.


### Release lifetime-black reruns — 2026-10-29

The newer Release acceptance supersedes the earlier 5/5 + 5/5 no-probe result
above as the current status. With physical Vulkan image-alias guards enabled,
`release-guarded` completed 2/5 caster-off and 2/5 caster-on renders; all
outputs decoded, and none of the guards fired. A separate run with Vulkan sync
validation completed 2/5 off and 3/5 on, with no `VUID-`, `SYNC-HAZARD`, or
validation-error log entries. These are acceptance failures despite successful
process exits and decode.

An async-upload timeline wait was added to normal and frame-batch submissions,
with an 8-iteration regression test (50 assertions) passing on the backend
registry target. It did not stabilize T05. Release snapshots show valid source
color surfaces but intermittent zero output already at Transform, and occasional
zero Composite output; the loss therefore is not localized only to Lighting.
The physical alias guards and sync validation did not identify a hazard. The
root cause is still unknown and T05 remains **PARTIAL — OPEN**.

Evidence: `docs/evidence/lights-shadows/T05-native-vulkan-20261029/`
(`acceptance-results-release-guarded.json`,
`acceptance-results-release-sync-check.json`, probe snapshots and
`diagnostic-release-synced-off-*.log`). The async upload regression is in
`tests/registry/test_backend_registry_vulkan_transforms.cpp`.

### Follow-up surface-lifetime audit — 2026-10-29

Reviewed the unplanned frame-batch route used by the failing render. Each
surface operation acquires leases for its source and destination before it
records commands; `resolve_image()` records the active frame slot for every
resolved handle; and `release_surface()` defers deletion while the frame batch
is active. Deferred deletion checks every recorded slot against in-flight and
currently-recording slots before erasing a binding. This rules out the simple
claim that an ordinary `release_surface()` frees a surface immediately after
one operation records it. It does not prove the entire graph ownership or
physical-slot history correct, and it does not explain the intermittent zero
Transform outputs. No lifetime fix is justified by this audit alone; T05
remains **PARTIAL — OPEN** pending a concrete reproducer/root-cause trace.


### Final no-probe acceptance and identity snapshot — 2026-10-29

The final five-run no-probe batch (`acceptance-results-final.json`) does not
reproduce the earlier 5/5 + 5/5 result: caster-off produced 4/5 complete
frames; run 5 decoded to mean RGB 5.0228/255 (max 191, 349,132/6,220,800
nonzero channels). Caster-on produced 3/5 complete frames; run 3 mean was
0.9409/255 (max 127) and run 5 mean 0.6592/255 (max 28). Every renderer and
decoder exited 0, so process success is not acceptance. The three complete
on-renders have mean RGB 53.5169/255.

A separate identity/snapshot probe of a successful caster-on render found
distinct source/destination `VkImage` objects and initialized images for the
observed transforms and Lighting operations. Source color, Lighting and final
composite snapshots had nonzero content; an empty Transform/DepthGrade chain
was transparent as expected. This healthy sample does not localize the failure
in the degraded no-probe runs, so it neither proves nor fixes the lifetime
race. The T05 lifetime-black root cause, stable 5/5 acceptance and shadow
quality review remain **OPEN**.

Evidence: `acceptance-results-final.json`, `acceptance-final-*.log/.mp4`, and
the opt-in snapshot log `/dev/shm/t05-final-id-on.log` (temporary local
diagnostic).


### Follow-up probe batch — 2026-10-29

Five caster-off renders with graph-edge, surface-snapshot, transform-dispatch,
and physical `VkImage` identity probes reproduced 1/5 fully black, 2/5
near-black/degraded, and 1/5 full-quality frames (probe mode alters timing, so
this is diagnostic only). In all cases, graph inputs resolved and color-source
snapshots were populated. The failing samples showed nonzero affine Transform
outputs followed by zero Lighting outputs for particular layers; the recorded
source and destination `VkImage` values were distinct, initialized, and had
matching layer extents, with nonempty dispatch rectangles. No Vulkan validation
error was present. The loss is now narrowed to content production/visibility at
the per-layer GPU pass boundary, but this evidence does not yet distinguish a
shader/descriptor execution issue from image synchronization or lifetime reuse.
No production fix is justified yet; T05 remains **PARTIAL — OPEN**.

The descriptor-binding follow-up was subsequently built and run with
`CHRONON3D_T05_DESCRIPTOR_DIAG=1` (log: `/dev/shm/t05-current-descriptor-probe.log`).
The three Lighting dispatches used distinct descriptor sets, source/destination
views, images, and VMA allocations; each source/destination pair had matching
extents and both images were marked initialized. `write_descriptors()` maps
binding 0 to the destination view and binding 1 to the source view, matching
`lighting.comp`. In this probe, Transform nodes 8/13/18 had nonzero content;
Lighting nodes 14/19 read back as zero while node 9 remained nonzero. This
rules out an obvious host-side source/destination swap or descriptor-set reuse,
but does not prove shader execution, device-side contents at dispatch, or
synchronization/lifetime correctness. The probe changes timing and is not an
acceptance run. The lifetime-black root cause and a stable 5/5 Release
acceptance remain open; T05 stays **PARTIAL — OPEN**.

### Final current-tree Release rerun after descriptor diagnostics — 2026-10-29

Rebuilt `chronon3d_cli` in `linux-video-release` from the current working tree
and reran the five caster-off plus five caster-on strict-native acceptance
renders without probes. All 10 renderer and decoder processes exited 0. The
acceptance classifier counted 3/5 complete off frames and 5/5 on frames;
off-run RGB means were 17.9229, 53.1802, 53.1802, 5.0228, and 53.1802, while
on-run means were 53.5299, 53.5169, 53.5169, 53.5169, and 51.6293. Against the
immediately prior no-probe batch (4/5 off, 3/5 on), this is −1 complete off
frame and +2 complete on frames. The result remains intermittent and does not
identify the lifetime-black cause; T05 stays **PARTIAL — OPEN**.

Evidence: `acceptance-results-current-release-20261029.json` and matching
`acceptance-current-release-20261029-{off,on}-*.log/.mp4` files.
The current-tree backend-registry Lighting focus also passes 3/3 cases and
69/69 assertions (`lighting-focused-current.log`); it covers directional
Lighting, alpha/multiplier parity, and a GPU-only clone in the same frame batch.
This unit-level success does not override the intermittent graph acceptance.
