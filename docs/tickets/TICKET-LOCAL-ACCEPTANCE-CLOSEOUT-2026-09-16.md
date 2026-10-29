# TICKET-LOCAL-ACCEPTANCE-CLOSEOUT-2026-09-16

**Stato:** ACTIVE

Obiettivo: portare il checkout corrente al massimo stato verificabile, senza
marcare `PASS` criteri non eseguiti. Questa checklist è il registro operativo:
ogni voce viene marcata solo dopo il comando o l'evidenza corrispondente.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **Completare il target aggregato `chronon3d_tests`** — logica: il aggregatore è la porta per l'esecuzione batch delle suite; va completato senza perdere asserzioni. Dove: CMake tests + `tools/gates/manifest.sh`. Gate: target che esegue tutte le suite registrate.
- [ ] **Esecuzione sullo stesso SHA: suite release/full-validation + matrice FFmpeg** — logica: `doctor`, codec/routing, native fail-closed; cert C ABI/SDK se la lane compila; cert video/GPU se l'host espone Vulkan/CUDA/NVENC. Gate: evidenza reale registrata (un test non eseguito resta `NOT RUN`).
- [ ] **Debito residuo aperto** — logica: (a) `Composition::SceneFunction` → lowering typed con equivalence corpus (catena C20–C23 di `TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT`); (b) chiudere i caller legacy di `SoftwareRenderer::runtime()`; (c) certificare le exit condition FFmpeg pipe (exit 3); (d) gate “feature assente = costo fisico zero”; (e) progettare `PixelProgram → DeviceProgram` senza nuova authority. Gate: per ogni voce, rispettivo gate documentato.
- [ ] **Certificazione same-SHA CI del `main` corrente** + **installare `cmake-format` ed eseguire `check_cmake_format.sh`**. Gate: CI green + check verde.

## 0. Repository e sincronizzazione

- [x] `main` verificato e sincronizzato con `origin/main`.
- [x] Nessun commit remoto mancante dopo `git fetch origin`.
- [x] Working tree iniziale pulito.
- [x] SHA registrato: `763501ecc`.

## 1. Checklist e audit statico

- [x] Checklist creata in questo ticket.
- [x] Audit dei tre demolition target: compatibility composition, software
      runtime surface, FFmpeg pipe encoder.
- [x] Audit di cache/registry/authority duplicate.
- [x] Audit di `PixelProgram`/`DeviceProgram`/Slang e feature-gating.
- [x] `git diff --check` PASS.
- [x] Shell syntax check PASS su tutti gli script `.sh`.
- [x] Python AST parse PASS su tutti i file `.py` del checkout.
- [x] `check_no_sqlite.sh` PASS.
- [x] `check_test_hygiene.sh` PASS.
- [x] `check_conflict_markers.sh` PASS con `TMPDIR` scrivibile.
- [x] `architecture_gate_selftest.sh` PASS: 27/27 assertion.
- [x] `check_architecture.py` PASS: 191/191 regole.
- [x] Documentation tool-reference gate PASS: 68/68 riferimenti.
- [ ] `check_cmake_format.sh`: non eseguito, manca `cmake-format`.

## 2. Correzioni locali

- [x] Corretto il falso verde del CMake DAG self-test: il checker non deve
      scartare fixture CMake perché la radice assoluta contiene `.tmp` o `build`.
- [x] Allineato `CURRENT_STATUS.md` al comportamento reale: il downgrade
      native→pipe è fail-closed; resta solo il percorso pipe esplicito.
- [x] Corretto il build break introdotto dagli include relativi di `sha256.hpp`:
      i due consumer ora usano l'header canonico `chronon3d/assets/sha256.hpp`.
- [x] Corretto il build break della lane video senza FFmpeg nativo: le API che
      usano `AVPacket` sono ora native-only, il fallback DirectYUV espone anche
      `resolve_support()`, e il report render non linka verification quando è
      disabilitato.
- [x] Corretto il build break senza CUDA/Vulkan: `device_vram_bytes` ha una
      definizione neutra fuori dal ramo hardware e resta a zero in modo
      esplicito quando il probing GPU non è disponibile.
- [x] Corretto il link break della suite CLI no-native: il test di dispatch
      `GopExecutionExecutor` ora è compilato solo insieme all'autorità nativa
      `MuxSession`; i test di policy CPU-safe restano attivi senza FFmpeg.
- [x] Verificato che la patch non introduca conflict marker, errori di sintassi
      o violazioni architetturali.
- [x] Corretto il contratto encoder dichiarato no-native: dimensioni note dalla
      richiesta restano conservate anche quando la materializzazione FFmpeg è
      assente.
- [x] Corretto il test IPC per usare `std::filesystem::temp_directory_path()`;
      non assume più che `/tmp` sia scrivibile.
- [x] Corretto anche il test helper di `RawVideoSink` per usare la directory
      temporanea standard dell'ambiente; i test non assumono più `/tmp` e i
      casi di directory invalida restano espliciti.
- [x] Riallineato il fixture C ABI al solo asset immagine tracciato in
      `tests/install_consumer/assets`.
- [x] Corretto il mapping C ABI degli errori compiler con path `assets.*` a
      `ASSET_NOT_FOUND`, preservando il payload strutturato dell'asset.
- [x] Resa la cancellazione SDK preflight-first: una richiesta già cancellata
      restituisce `CANCELLED` prima dell'apertura dell'encoder e non può essere
      mascherata da un backend video non disponibile.
- [x] Estesa la verifica di integrità degli asset preparati al percorso
      `CompiledProgram`/C ABI prima del decode; la protezione TOCTOU ora vale
      sia per `CompiledComposition` sia per il programma canonico.
- [x] Corretto il test TBB per usare il fixture immagine tracciato
      `assets/images/checker.png` invece di un asset inesistente.
- [x] Estesa la discovery del CLI nel test di determinismo al preset
      `linux-ci`.
- [x] Chiusa la lacuna del manifest SDK: installato anche il compatibility
      include pubblico `chronon3d/core/error.hpp`, richiesto da
      `sdk/render_error.hpp` e da header runtime transitivi, insieme al suo
      header canonico `core/types/error.hpp`.
- [x] Scansionata la chiusura degli include del prefisso SDK e aggiunti al
      manifest gli header transitivi mancanti per camera motion, preset,
      frame parameters, rendering lighting/depth e runtime frame/GPU plan;
      la scansione incrementale ha inoltre rilevato e aggiunto la seconda
      frontiera di dipendenze (shadow, motion animation, execution decision,
      render surface e resource plan/transition), poi la chiusura finale di
      motion state e resource descriptor/state. Corretto anche l’ordine di
      link ICU nel text core per i consumer statici e aggiunto
      `runtime/resource_residency.hpp`.
- [x] Chiuso il contratto `ResourceStateTracker`: le prime scritture su
      subresource disgiunte non emettono barriere spurie, gli overlap multipli
      vengono aggregati solo con layout/queue compatibili, e il confronto con
      `GpuCommandPlanner` riproduce anche i confini di alias fisico.
- [x] Corretto il gating della suite animation: i test delle composizioni
      builtin vengono registrati solo con `CHRONON3D_BUILD_CONTENT`; la lane
      no-content esegue comunque i test delle curve video senza riferimenti
      impossibili a `GridCleanBackground`.
- [x] Corretto il backface culling della camera 2.5D: le superfici 2D
      proiettate (grid, testo e immagini) sono double-sided, mentre la
      geometria native 3D conserva il culling fisico.
- [x] Resi robusti i fixture C ABI install-consumer: il piano missing-asset
      dichiara la size necessaria per raggiungere il preflight e i test video
      riconoscono il backend `video sink unavailable` come skip no-native.
- [x] Corretto il percorso SDK non-modular: un layer 2.5D projected mantiene
      sempre il `TransformNode` necessario a consumare la homography camera;
      la policy `modular_coordinates=false` non può più lasciare il source
      deferito senza stage di trasformazione.
- [x] Migrato il consumer asset installato dalla facade `authoring/*` rimossa
      al contratto canonico `assets::AssetRef` + `SceneBuilder`; il gate non
      reintroduce header legacy inesistenti e verifica comunque isolamento
      delle roots per immagini/font e CWD indipendente.
- [x] Eseguito il rerun install-consumer con diagnostica estesa: il fallimento
      camera/full ha escluso la matrice camera come causa primaria e ha
      evidenziato l'`EarlyExit` dei nodi bound nel percorso compatibility.
- [x] Corretto il confine compatibility/typed: `make_graph_context()` non
      espone più il payload typed vuoto di `FrameExecution`; i nodi ricevono
      `frame_parameters` solo quando il percorso compilato li fornisce davvero.

## 3. Toolchain e build locale

- [x] Reso disponibile un `vcpkg.cmake` compatibile con il baseline del
      manifest.
- [x] Configurato `linux-fast-dev`.
- [x] Compilato il target `chronon3d_dev_fast` con `-j10`: 515/515.
- [x] Eseguire i test core/scene/CLI del preset fast: core 1480/1480 test
      case e 954981/954981 assertion; scene, CLI e render-job PASS.
- [x] Configurare e compilare la lane `chronon-lean` no-content con `-j10`:
      configure PASS e build PASS dopo la correzione della compilazione video
      non-native; 522 step complessivi.
- [x] Eseguire `ctest` sui 12 binari freschi disponibili: 12/12 PASS con
      `TMPDIR` scrivibile e `-j10`; il wiring dell'executor per PrecompNode è
      stato corretto e il test precomp ora passa 14/14.
- [x] Eseguire il test SDK install-consumer dopo la correzione del confine
      compatibility/typed: installazione, canary 10/10, text, grid, camera,
      full, C ABI (12 PASS/1 SKIP) e typed asset consumer PASS.
- [ ] Completare il target aggregato `chronon3d_tests`: il target build è stato
      riprovato sul `linux-fast-dev` corrente ma non ha restituito entro 600 s;
      non viene dichiarato PASS. Tuttavia il CTest registrato sullo stesso
      build directory ha eseguito **89/89 test PASS** serialmente, senza
      ricompilare né creare pressione di memoria.
- [x] Eseguire i gate developer registrati in `tools/gates/manifest.sh`: 10/10
      PASS con `TMPDIR=.tmp` scrivibile; il gate steady-state segnala solo
      evidenza storica su una revisione precedente.
- [x] Rerun mirato sul tree dirty `a214fad27efdaa597ccf4e4a8ae34baa6677146e`
      (2026-10-29): dopo due failure `No space left on device` sul relink di
      archivi da 2–3 GiB, il solo archivio statico SDK incompleto è stato
      ricreato in `/dev/shm` usando la lista oggetti generata da Ninja e
      riagganciato al build tree; nessun sorgente o artefatto preesistente è
      stato rimosso manualmente. Exit 0 per build e link dei dieci target
      core, backend registry, CLI, media video, zero-copy, CUDA NV12, particle,
      render graph, effects e text-tile canary; CLI eseguibile ricostruita
      separatamente (exit 0). CTest: 9/10 PASS iniziale; il solo failure era
      l'aspettativa obsoleta di 48 effetti. Aggiornato il census: 57 catalogati,
      51 native, 6 esplicitamente fail-closed; il test ora controlla il set
      esatto dei 6. Rerun `chronon3d_render_graph_tests` PASS, 433/433 casi e
      19335/19335 assertion. Gli altri nove CTest della batch sono PASS
      (core 37.21 s, backend registry RTX A4000 226.20 s, CLI 65.43 s,
      media-video 28.25 s, zero-copy 0.16 s, CUDA NV12 7.48 s, effects
      10.13 s, particle 0.15 s, text-tile 0.84 s). L'aggregato
      `chronon3d_tests`, SDK C ABI on, T05 acceptance e benchmark B2 restano
      separati/non ancora eseguiti in questo batch.

## 4. Acceptance e certificazione raggiungibile

- [ ] Eseguire la suite release/full-validation sullo stesso SHA: NOT RUN in
      questo pass, perché il filesystem è al 98% (6.5 GiB liberi).
- [ ] Eseguire la certificazione C ABI/SDK consumer se la lane compila: BLOCKED
      localmente; `linux-fast-dev` e `linux-video-release` hanno
      `CHRONON3D_BUILD_C_API=OFF` e non contengono `libchronon3d_c.so`.
- [ ] Eseguire la matrice FFmpeg disponibile (`doctor`, codec/routing,
      native fail-closed).
- [ ] Eseguire la certificazione video/GPU se l'host espone Vulkan/CUDA/NVENC
      e il content pack richiesto.
- [ ] Registrare evidenza same-SHA per ogni suite realmente eseguita.
- [ ] Aggiornare lo stato solo con `PASS`, `FAIL`, `PARTIAL` o `NOT RUN`.

## 5. Demolition debt: solo dopo le exit condition

- [ ] `Composition::SceneFunction` → lowering typed con equivalence corpus;
      poi rimuovere `compatibility_composition`.
- [ ] Chiudere i caller legacy di `SoftwareRenderer::runtime()`;
      poi completare `SoftwareExecutionContext` e rimuovere il back-pointer.
- [ ] Certificare tutte le exit condition FFmpeg pipe;
      poi rimuovere il percorso pipe esplicito.
- [ ] Aggiungere/chiudere il gate “feature assente = costo fisico zero”.
- [ ] Progettare `PixelProgram → DeviceProgram` senza una nuova authority di
      cache, registry, scheduler o executor.
- [ ] Chiudere la certificazione same-SHA CI del `main` corrente.

## Stato di chiusura

- **Completato localmente:** sezioni 0, 1 e 2, salvo il gate `cmake-format` non
  eseguibile per dipendenza mancante.
- **Raggiungibile ma ancora da eseguire:** target aggregato `chronon3d_tests`
  come build target (il CTest equivalente è già 89/89 PASS).
- **Bloccato dalle risorse/configurazione attuali:** SDK C ABI install-consumer;
  serve una configurazione `CHRONON3D_BUILD_C_API=ON` e spazio libero sufficiente.
- **Condizionato da host/CI/content pack:** la parte GPU/video della sezione 4.
- **Non autorizzato prima delle equivalence:** sezione 5.


## Local aggregate target recheck — 2026-10-29

The aggregate `chronon3d_tests` target is present in
`tests/manifests/test_aggregates.cmake`, but a current-tree build attempt on
`linux-video-fast-dev` did not complete. Command:

```text
cmake --build build/chronon/linux-video-fast-dev -j10 --target chronon3d_tests
```

Ninja stopped while relinking `libchronon3d_cli_video_export.a`; GNU `ar`
reported `No space left on device`. The filesystem had 3.7 GiB free (99% used)
and the build directory already occupied 44 GiB. Independent compile steps
continued briefly, but the aggregate returned failure at 23/448 steps. No tests
were run by this target attempt. The previous 89/89 CTest result on
`linux-fast-dev` is historical evidence from a different configured tree and
does not turn this build attempt into a pass. Keep this item open until the
aggregate can be built and its registered suites executed on the current tree.
