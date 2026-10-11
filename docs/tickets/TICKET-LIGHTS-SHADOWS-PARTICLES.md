# TICKET-LIGHTS-SHADOWS-PARTICLES — luci reali, ombre portate, particelle veloci

Stato: **PARTIAL — OPEN** (scope scelto dall'utente il 2026-10-08: luci reali con ombre
portate, e istanziamento GPU per le particelle).

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

### Verifica GPU Windows (2026-10-11)

- La RTX 4060 Ti esegue correttamente un kernel CUDA verificato elemento per
  elemento. Nel checkout isolato Windows, dopo aver inizializzato MSVC, la
  build compila i sorgenti core modificati e genera gli shader, ma non linka
  ancora il CLI: il blocco attuale è l'uso diffuso di `__int128`, non supportato
  da MSVC, a partire da `core/types/time.hpp`; anche alcune funzioni
  `constexpr` chiamano `std::abs`, non constexpr in questa toolchain. Il
  renderer Chronon non è quindi stato eseguito sulla RTX. Evidenza:
  `docs/evidence/windows-rtx-4060ti-cuda-smoke-2026-10-11.md`.
- Un percorso Windows `VirtualAlloc` è stato aggiunto a `memory_utils.hpp` e
  verificato con smoke MSVC; sono stati aggiunti i rami Windows per tempo,
  restrict e cancellazione segnale. WSL 2 vede la RTX via CUDA ma Vulkan
  seleziona solo `llvmpipe`, quindi non certifica il renderer GPU.
- La parità Vulkan per ombre/particelle resta aperta; il test CUDA non la
  sostituisce.

### Verifica CPU Linux (2026-10-11)

- `chronon3d_render_graph_tests --test-case='PR2-*Shadow*'`: 6/6 casi e 18/18
  asserzioni. Copre la cache key e i limiti del bounding box di `ShadowNode`,
  oltre ai test software legacy per la proiezione/blur `DropShadow` e il render
  senza effetto. Non è una prova end-to-end del nuovo percorso
  `LightContext` → receiver → `ShadowNode`.
- `chronon3d_renderer_core_tests --test-case='Shadow*'`: 14/14 casi e 37/37
  asserzioni; include calcoli di illuminazione, builder, impostazioni e test
  software delle ombre della scena. Questi test non certificano la parità
  Vulkan né coprono integralmente le luci RenderPlan introdotte in questa
  tranche.
- `chronon3d_renderer_core_tests --test-case='Point light shadow is composited onto an accepting receiver'`:
  1/1 caso e 20/20 asserzioni. Attraversa il GraphBuilder CPU con un
  `PointLightSource` nel `LightContext`: `casts_shadow=true` cambia il
  framebuffer del receiver, `casts_shadow=false` lo rimuove, e un receiver con
  `accepts_shadows=false` resta identico in entrambi i casi. Con soglia colore
  0,25, la sagoma misurata è `[214,30 → 294,110]`, cioè 80×80 px rispetto
  all'atteso, entro 1 px. Il bias 8 sposta il bbox di `(5,-4)` px; raggio 4
  allarga il bbox rispetto al raggio 0.
- B1–B3 hanno ora evidenza d'integrazione CPU per il caso point-light. Il test
  usa `PointLightSource` direttamente e non attraversa decoder/compiler
  RenderPlan. In questa verifica iniziale spot/directional e fail-closed
  Vulkan/parità GPU restavano separati. Il ticket complessivo resta **OPEN**
  anche per il batching particelle.
- La prova locale è stata estesa a sorgenti `Spot` e `Directional`: il test
  `Spot and directional light shadows reach accepting receivers` passa 1/1
  caso e 24/24 asserzioni su CPU. Per entrambi verifica che un receiver
  accettante cambi rispetto al baseline senza ombra e che `accepts_shadows=false`
  resti pixel-equivalente al baseline; il bbox a soglia 0,25 e la dimensione
  80×80 px coincidono con l'oracolo entro 1 px. Non attraversa il compiler
  RenderPlan e non sostituisce la parità Vulkan, che resta aperta.
- `Spot light outside its cone does not cast onto the receiver` passa 1/1
  caso e 3/3 asserzioni: spot puntato lontano dal caster produce un framebuffer
  identico al baseline senza ombra.
- Suite combinata `--test-case='*Shadow*,Spot light outside*'`: 17/17 casi,
  84/84 asserzioni. Tutte le verifiche sono software CPU.

### RenderPlan receiver opt-in — fix CPU 2026-10-11

- L'audit aveva trovato che la policy `enable_3d` sovrascriveva sempre
  `accepts_shadows(false)` dopo aver applicato i flag `LayerPlan`. Ora il
  compiler conserva `accepts_shadows=true` esplicito e mantiene il default off
  quando il flag è assente o falso. Le Shape 3D restano non illuminate, come
  prima; l'opt-in riguarda solo il passaggio ombre.
- Aggiunta la regressione
  `RenderPlan preserves explicit shadow opt-in on 2.5D shape receivers`: il
  test valuta il `CompiledComposition` e controlla sia l'opt-in sia il default.
  Build mirato `chronon3d_renderer_core_tests` passato; filtro ombre + nuovo
  caso: **18/18 casi, 86/86 asserzioni PASS**.
- Aggiunta anche `RenderPlan point spot and directional shadows reach 2.5D
  receivers`: costruisce luce, caster e receiver dal RenderPlan, renderizza il
  `CompiledComposition` con `SoftwareRenderer` e confronta i frame per Point,
  Spot e Directional. Per tutti e tre, il receiver opt-in differisce dal
  baseline senza shadow; quello con `accepts_shadows=false` non presenta
  differenze oltre la soglia `1e-3` dal proprio baseline.
- Build mirato aggiornato e filtro combinato ombre/receiver: **19/19 casi,
  104/104 asserzioni PASS**. Le tre route hanno ora prova CPU end-to-end;
  parità Vulkan su hardware NVIDIA, fail-closed strict GPU e istanziamento GPU
  particelle restano aperti.

### Re-run Vulkan software — 2026-10-11

- `vulkaninfo --summary` seleziona `llvmpipe (LLVM 21.1.8, 256 bits)` con
  `PHYSICAL_DEVICE_TYPE_CPU`; non è la RTX né un risultato GPU hardware.
- Sul binario `chronon3d_backend_registry_tests` sono passati
  `Strict Vulkan DropShadow matches software and invalid parameters fail closed`
  e `Vulkan lighting preserves alpha and matches the CPU multiplier oracle`:
  **2/2 casi, 543/543 asserzioni**.
- È evidenza del percorso Vulkan eseguito dal driver software e dei relativi
  oracle CPU. Non attraversa il nuovo percorso receiver-owned
  `LightContext` → `ShadowNode`, non certifica strict-mode GPU su NVIDIA e non
  verifica particelle Vulkan; tali criteri restano aperti.

### Verifica batching CPU particelle (2026-10-11)

- `chronon3d_c_abi_tests --test-case='render plan materializes deterministic particle shapes per frame'`:
  1/1 caso, 38/38 asserzioni. Il test verifica campionamento deterministico
  per-frame e un solo nodo `Path` per un emettitore opaco a colore uniforme.
- Il compiler raggruppa le particelle solo quando sono opache, usano blend
  normale e non hanno sprite; crea un path per ciascuna sequenza contigua di
  colori. Le particelle traslucide, additive o con sprite restano per-nodo per
  preservare il compositing attuale.
- Questo è batching CPU parziale. Il criterio C1 resta **OPEN**: manca la prova
  di un solo draw per emettitore e di output byte-identico al percorso per-nodo
  sul corpus richiesto. C2 (benchmark 1k/10k/100k) e C3 (istanziamento Vulkan
  e parità GPU) non risultano soddisfatti.

### Re-run del campionatore CPU — 2026-10-11

Il sorgente mantenuto `tests/scene/shapes/test_particles_sampling.cpp` è stato
ricompilato come doctest indipendente contro la libreria `xxhash` del preset
locale e rieseguito: **7/7 casi e 77.169/77.169 asserzioni passati**. La prova
include ora il budget completo di 4.096 particelle, il limite calcolato di
64 MiB, l'emettitore vuoto e l'uguaglianza campo-per-campo del campionamento
deterministico dopo un accesso a un altro frame. È una verifica del campionatore
CPU, non del target CMake, del batching/draw per emitter o dell'instanziamento
Vulkan. I criteri C1–C3 restano aperti.

### Diagnosi parità del batching CPU — 2026-10-11

- Un confronto end-to-end sullo stesso frame, seed e campionatore ha messo a
  confronto il path aggregato del RenderPlan con otto `Circle` per-nodo. Il
  test ha confermato un solo `Path` aggregato, ma il confronto byte per byte è
  fallito: hash `4808501314955389786` contro `12185350198877912278`; 29 pixel
  differiscono su 16.384, con errore massimo di canale 1 e bbox
  `[9,70 → 86,81]`.
- La causa è nei rasterizer CPU correnti: `Circle` usa hit-test analitico
  binario per pixel, mentre `Path` passa da Blend2D con copertura antialias.
  Quindi questo batching cambia i pixel dei bordi e **non soddisfa C1**;
  conteggio nodi e determinismo non bastano a dichiarare la parità.
- La prova è stata usata come diagnosi mirata e rimossa dal target di test,
  perché codificarne il fallimento come test verde nasconderebbe il requisito
  non soddisfatto. La correzione richiede un percorso batch che preservi la
  rasterizzazione analitica, oppure una decisione esplicita sul contratto di
  parità; fino ad allora C1 resta **OPEN**. C2 e C3 restano invariati.

### Tranche animazione posizione/direzione — candidato locale

La tranche è ora presente su `main` a `e8e06f5b3`. La certificazione Actions
su quello SHA resta bloccata prima dell'avvio dei runner; i risultati locali
registrati sotto non sostituiscono una nuova verifica clean-SHA.

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

### Verifica CUDA su Windows — 2026-10-11

- La RTX 4060 Ti è stata verificata via SSH/Tailscale da `Youtube_King`:
  driver 610.88, 8188 MiB, CUDA Toolkit 12.6. Un kernel CUDA ha sommato due
  vettori da 1.048.576 elementi e verificato tutti i risultati; output:
  `CUDA_KERNEL_PASS device=NVIDIA GeForce RTX 4060 Ti count=1048576 sample=3.75`.
- Per il solo smoke test `nvcc` ha richiesto `-allow-unsupported-compiler`,
  perché il PC ha Visual Studio 2026 mentre CUDA 12.6 dichiara supporto fino a
  Visual Studio 2022. Il calcolo GPU è riuscito, ma questa combinazione non
  certifica una build Chronon3d di produzione.
- Evidenza completa: [`windows-rtx-4060ti-cuda-smoke-2026-10-11`](../evidence/windows-rtx-4060ti-cuda-smoke-2026-10-11.md).
- La verifica prova che la GPU Windows esegue kernel CUDA; non prova la parità
  Vulkan di `ShadowNode`, né il rendering luci/particelle di Chronon3d sulla
  GPU. Tali criteri restano aperti.

### Re-run mirato delle tracce luce CPU — 2026-10-11

Sul binario `chronon3d_c_abi_tests` già ricostruito dal worktree corrente sono
stati rieseguiti lowering point/spot/directional, campionamento deterministico
di posizione/direzione e tracce intensità/colore: **4/4 casi, 152/152
asserzioni PASS** (`--test-case='render plan lowers point spot*,RenderPlan
source lights*,RenderPlan light direction*,RenderPlan light intensity*'`).
È evidenza CPU della route di compatibilità RenderPlan; il typed compiler,
le ombre Vulkan e il batching/instanziamento particelle restano fuori da questa
verifica.

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
