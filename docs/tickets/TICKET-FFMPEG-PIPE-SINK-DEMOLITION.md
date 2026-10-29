# TICKET-FFMPEG-PIPE-SINK-DEMOLITION — delete subprocess encoder fallback

**Stato:** BLOCKED (explicit diagnostic pipe encoder remains; native production certification gates are not green)
**Priorità:** P1 demolition debt  
**Authority:** `src/media/video/video_sink_factory.cpp` + `src/media/CMakeLists.txt`

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **Exit condition 3 — CI green codec/routing** — logica: la coverage richiesta esiste (`tests/video/test_native_av_sink_factory.cpp`, `test_native_av_sink_lifecycle.cpp`, lane `Native Video Certification`); manca solo l'evidenza CI green sullo SHA pubblicato (i run su `be6b4dc3a` sono in corso; le modifiche locali non sono coperte). Gate: codec/routing coverage green sullo stesso SHA.
- [ ] **Poi: cancellazione fisica** — logica: solo dopo exit 3. Dove: `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp` + `VideoSinkEncoderAdapter(Ffmpeg)` + ramo `#else` di `VideoSinkFactory` (`src/media/video/video_sink_factory.cpp`), target pipe in `src/media/CMakeLists.txt`, doc/options `ffmpeg-mode pipe`. Gate: gate anti-resurrezione F40 esteso ai file rimossi + `check_architecture.py` green + commit SHA con evidenza WBH/CI.

### Stato CI osservato 2026-10-28

- Sul precedente SHA `cfdd5b15d`, `chronon3d-runtime` falliva compilando il pin ChrononPlugin
  `c000ee3`, dove `plugins/maps/src/map_camera.cpp` usa `std::int64_t` senza
  `<cstdint>`. Il worktree locale ha già avanzato il pin Docker a `5e33bd0`, il
  commit ChrononPlugin che aggiunge l'include; la singola unità locale passa
  `c++ -std=c++20 -fsyntax-only`. Questo fix è stato pubblicato su Chronon3D
  `main` come `be6b4dc3a`.
- I run sul nuovo SHA `be6b4dc3a799fe3b38fe6e54e7bddcae5a60cd5e`, inclusa la
  lane [Native Video Certification](https://github.com/Marcuss-ops/Chronon3d/actions/runs/40663388401),
  sono ancora in corso. L'exit 3 resta aperta finché la coverage codec/routing
  non conclude green sullo stesso SHA.
- Le modifiche locali di questa tranche non sono coperte da quei run. Exit 3
  resta aperta: non rimuovere ancora l'adapter/target pipe.

## 2026-09-04 negative census (authoritative)

| Target | Stato | Evidenza |
|---|---|---|
| `src/media/video/ffmpeg_pipe_sink.*`, `ffmpeg_pipe_args.cpp`, `ffmpeg_frame_submit.cpp`, `process_runner.*` | **GIÀ DEMOLITI** | 0 file in `src/`, `apps/`, `include/` |
| `FfmpegPipeSink` symbol in production code | **ESTINTO** | solo 2 commenti storici in `include/` (aggiornati) |
| `ProcessRunner` | **ESTINTO** | 0 match in src/apps/include/tests |
| `bind_unplanned` (Vulkan surface authority) | **VIVO — non eliminare ora** | `vulkan_surface_authority.hpp` (4 ref); entry point: `vulkan_surface_store_private.cpp:65 ensure()`. Richiede Wave 1 sync authority, non demolition stop-and-pop |
| **Residuo reale: pipe ENCODER lato CLI** | **VIVO, esplicito** | `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp` + adapter usato solo quando `encoder_backend=pipe`; un native richiesto ma non disponibile fallisce chiuso |

## Nuovo scope residuo (sostituisce la lista file sopra)

1. **Downgrade silenzioso nativo→pipe**: **RISOLTO 2026-09-08**. `create_video_encoder()` con `CHRONON3D_ENABLE_NATIVE_FFMPEG` spento e `encoder_backend == "native"` restituisce `nullptr` dopo un errore esplicito; nessun output può essere prodotto dal percorso pipe.
2. **`--encoder-backend pipe` come percorso esplicito**: resta consentito come modalità diagnostic/debug dichiarata (null/adaptive), ma mai come fallback silenzioso del native.
3. Cleanup finale di `ffmpeg-mode pipe` doc/options dopo certificazione del percorso native, zero caller di produzione e zero fallback.

## Census 2026-10-23 (SHA `f935e3e32`) — exit conditions 2, 4, 5 verificate

Repo-wide census re-run at the reconciled main SHA (audit follow-up):

| Check | Result at `f935e3e32` | Evidenza |
|---|---|---|
| `FfmpegPipeSink` / `ProcessRunner` symbols in `include/`,`src/`,`apps/`,`tests/` | **0** (sole menzioni: docs) | repo grep |
| `ffmpeg_pipe_sink.*`, `ffmpeg_pipe_args.*`, `ffmpeg_frame_submit.*`, `process_runner.*` files | **0** | repo grep |
| `create_video_encoder` production callers | 2: `pipe_export_pipeline_setup.cpp:214` (child variant fanout) e `:231` (session encoder), entrambi dietro `encoder_backend` esplicito | grep + `video_export_common.cpp:12-58` |
| Silent native→pipe downgrade | **assente**: default `encoder_backend = native` (`encoder_options.hpp:71`); il decode IPC (`daemon_service_ipc_protocol.cpp:263`) può restituire `""` ma l'unico consumer (`command_render_plan_internal.cpp:338-339`) sovrascrive solo se non-vuoto; build senza libav + richiesta native = `nullptr` fail-closed | code trace |
| Production presets (RenderingGen motion-certification runtime reports, 13 file) | `"encoder_backend": "native"` in tutti | grep |
| `video_pipe_fallback_frames` in strict-native sidecars | **0** (T16 fullres, three-depth, shutter A/B ×2) | `docs/evidence/camera-authority-migration/T16/**.timing.json` |
| SDK consumers che producono video compresso | **nessuno** (01_cpp_minimal, 02_c_minimal, 02_content_registry, 03_go, 04_rust, 05_python: zero riferimenti a video encode / RENDER_JOB) | grep `sdk_consumers/` |

Nota semantica: `video_pipe_fallback_frames` (`video_export_sink.cpp:308-311`) conta
i frame codificati fuori dal lane GPU-native (frame software), NON l'uso del
trasporto pipe: un render software legittimo riporta 150/150 (es. sidecar
CAM-04), un render strict-native riporta 0. L'exit condition 4 va letta come
“purezza del lane GPU canonico”, ed è verificata.

**Exit 5 — SATISFATTA**: nessun target di produzione o SDK include/istanzia
`FfmpegPipeSink`, `ProcessRunner` o pipe args (estinti). Il residuo vivo resta
solo la modalità diagnostica esplicita `--encoder-backend pipe`
(`VideoSinkEncoderAdapter(Ffmpeg)`), consentita dallo scope §2 e non coperta
dall'exit 5.

**Exit 4 — SATISFATTA**: preset di produzione a zero pipe caller (tutti
`native`) e contatore `video_pipe_fallback_frames = 0` su tutti i run
canonici strict-native campionati.

**Exit 2 — SATISFATTA (insieme vuoto)**: nessun consumer SDK supportato produce
video compresso, quindi nessuno richiede il subprocess fallback; la clausola
“verificati sul percorso NativeAvSink” è vacua sull'insieme vuoto e resta tale
finché un consumer non acquisisce export video.

**Exit 3 — ANCORA APERTA**: la coverage codec/routing esiste
(`tests/video/test_native_av_sink_factory.cpp`, `test_native_av_sink_lifecycle.cpp`,
lane `Native Video Certification`) ma l'evidenza CI green è irraggiungibile
finché i runner GitHub non allocano (TICKET-MAIN-TRUTH-STAGE-1-2). La
demolizione fisica resta quindi bloccata, come previsto.

**F40 anti-resurrection gate — ATTERATO**: con exit 4+5 PASS,
`tools/architecture_rules_cleanup.toml` ora vieta `forbidden_path` sui file
pipe/process-runner demoliti e `forbidden_symbol` su `FfmpegPipeSink` /
`ProcessRunner` in `include/`,`src/`,`apps/` (skip_comments). Gate canonico
`tools/check_architecture.py`: **GATE_PASS 209/209** sullo stesso SHA.

## Blocco corrente — 2026-10-03

- La CI GitHub per `Chronon3d@5d960421dd1b` ha chiuso tutti i job prima
  dell'allocazione di un runner; senza CI same-SHA verde non si può certificare
  la demolition. I run locali non sostituiscono la certificazione GitHub.
- Il path pipe ha ancora caller espliciti e autorizzati per diagnostica. Non
  cancellare i file o le opzioni finché le exit conditions della scheda non
  sono passate.

## Problema (storico)

`FfmpegPipeSink` era una compatibility path basata su processo esterno. Nei build con native FFmpeg, l'authority canonica per output compresso è `NativeAvSink` e il muxing è in-process.

Il fallback pipe non deve diventare una seconda authority permanente.

## Regola architetturale

Per output video compresso di produzione:

```text
VideoSinkFactory
  -> NativeAvSink
  -> libavcodec / MuxSession / libavformat
```

Il subprocess encoder non è una production fallback silenziosa e non può riacquisire ownership su codec/muxing nei build native.

## Exit conditions — tutte obbligatorie

La demolition può partire solo quando sono vere tutte le condizioni seguenti:

1. [x] **`linux-video-release` rende native FFmpeg obbligatorio.** Il preset/release profile abilita `CHRONON3D_ENABLE_NATIVE_FFMPEG` e fallisce in configurazione se il backend native richiesto non è disponibile. `36c013bd05402cda98554e67d369687ab4587987` rende header e librerie libav requisiti `REQUIRED` a configure-time.
2. [x] **SDK consumers certificati.** I consumer supportati che producono video compresso sono verificati sul percorso `NativeAvSink`; nessun consumer supportato richiede il subprocess fallback. — verificato 2026-10-23 (census `f935e3e32`): l'insieme dei consumer che producono video compresso è vuoto.
3. [ ] **Contratti codec/routing verdi.** Test H264, H265/HEVC e routing raw/uncompressed sono verdi sul build native, inclusi open/submit/flush/close e teardown. La coverage richiesta è ora presente (`029547d2222d7a7730dc1d08c09f4bcf9e0ceed5`, `908de2eaf9159db3b206c1f8d242ad8bfaa5bd1e`, `2fb01f97dcd326714615bf2c3907e93860b35d3c`) e la lane dedicata è `Native Video Certification` (`16f3ffaf36f5b1b946fd69ad263d8ea2347656cf`); resta da acquisire evidenza CI green (runner GitHub non allocano — fuori portata in checkout locale).
4. [x] **Production presets a zero pipe caller.** I preset di produzione non selezionano `FfmpegPipeSink`; telemetry/contract runs non mostrano `video_pipe_fallback_frames` nel percorso canonico. — verificato 2026-10-23: 13/13 runtime report `encoder_backend=native`; `video_pipe_fallback_frames = 0` su tutti i sidecar strict-native campionati.
5. [x] **Caller census finale = zero.** Nessun target di produzione o SDK supportato include/istanzia direttamente `FfmpegPipeSink`, `ProcessRunner` o pipe args. — verificato 2026-10-23: simboli e file estinti; gate anti-resurrezione F40 atterrato (`check_architecture.py` 209/209).

## Demolition action

Quando le exit conditions sono certificate:

1. rimuovere il ramo `#else` di `VideoSinkFactory` per output compresso;
2. rendere il native backend una precondizione esplicita del build che abilita video compresso;
3. cancellare i file pipe/process-runner elencati sopra;
4. rimuoverli da `src/media/CMakeLists.txt` e dai test dedicati al fallback;
5. mantenere i test che bloccano `NativeAvSink` come authority canonica;
6. chiudere questo ticket con commit SHA + evidenza WBH/CI.

## Non-obiettivi di questo ticket

- Non cancellare il fallback prima che i consumer supportati siano certificati.
- Non introdurre un secondo registry/factory globale per tenere in vita il pipe path.
- Non reimplementare codec o muxing fuori dalle authority native esistenti.

## F37-F40 — caller census + codec matrix — 2026-09-09

- **F37 census — FATTO (repo grep 2026-09-09):** unico entrypoint `create_video_encoder(FfmpegPipeOptions) → IVideoEncoder`
  in `video_export_common.cpp:11` con 2 caller production: `pipe_export_pipeline_setup.cpp:169` (child variant fanout)
  e `:181` (session encoder). `FfmpegPipeSink`/`ProcessRunner`/`ffmpeg_pipe_sink.*`/`ffmpeg_pipe_args.*`/`ffmpeg_frame_submit.*` = 0 file
  in `src/apps/include` (già demoliti); `FfmpegPipeSink` symbol = 0 in production (solo 2 commenti storici). Residuo vivo
  è solo il ramo encoder `VideoSinkEncoderAdapter(VideoSinkType::Ffmpeg)` quando `encoder_backend != "native"` (explicit diagnostic),
  mai come silent downgrade del native (fail-closed dal 2026-09-08).
- **F38 codec/output matrix — FATTO (code मौजूद):** `video_config_validator.cpp` rifiuta H264/H265 su WebM, `native_av_sink.cpp`
  mappa `H264/H264Nvenc / H265/HEVC / AV1` su `AV_CODEC_ID_*`, `video_execution_resolver.cpp` default `h264`, `output_contract.cpp`
  fallback `h264`, `native_video_frame_decoder_session.cpp` cerca `h264_cuvid/hevc_cuvid/av1_cuvid` per NVDEC, `video_export_common.cpp`
  normalizza `h264→h264_nvenc, hevc→hevc_nvenc, qsv/vt/amf→h264_*`. Output: mp4/webm/mkv via `detect_container()` + raw/NV12/YUV420P.
- **F39 zero caller gate — APERTO (correctly blocked):** exit 4 (`video_pipe_fallback_frames == 0` in produzione) + exit 5
  (zero direct `FfmpegPipeSink`/`ProcessRunner` include) intenzionalmente aperte; demolition fisica bloccata finché
  certificazione native/SDK/codec non è verde. Non rimuovere prima.
- **F40 gate anti-resurrezione — FATTO (architecture):** `tools/architecture_rules_cleanup.toml` blocca `RenderServices`
  (service-locator) + `src/media/CMakeLists.txt` mantiene pipe/process-runner solo dietro ramo non-native; quando exit 4+5
  saranno PASS, aggiungere `forbidden_symbol` su `FfmpegPipeSink`/`ProcessRunner` in `src/apps/include` + `forbidden_path`
  sui file pipe — reintroduzione diventa gate FAIL.

## Evidenza corrente

- `VideoSinkFactory` seleziona `NativeAvSink` nei build con `CHRONON3D_ENABLE_NATIVE_FFMPEG`.
- Nei build senza `CHRONON3D_ENABLE_NATIVE_FFMPEG`, una richiesta esplicita
  `encoder_backend=native` è fail-closed (`create_video_encoder() == nullptr`);
  il comportamento è coperto da un test CLI condizionato al build; il test
  locale `chronon3d_cli_tests --test-case='native encoder request fails closed*'`
  è verde su `linux-fast-dev` (2026-09-08).
- `linux-video-release` eredita `CHRONON3D_ENABLE_NATIVE_FFMPEG=ON` e, da `36c013b`, la configurazione native fallisce immediatamente se header o librerie libav richieste non sono disponibili.
- `tests/video/test_native_av_sink_factory.cpp` blocca esplicitamente H264 e H265 su `NativeAvSink` e mantiene raw su `RawVideoSink`.
- `tests/video/test_native_av_sink_lifecycle.cpp` esercita H264 e H265 attraverso `open -> submit -> flush -> close` e teardown del sink reale.
- `.github/workflows/native-video.yml` costruisce ed esegue il target native dedicato su Linux; l'evidenza green deve essere registrata prima di marcare l'exit condition 3 come completata.
- `src/media/CMakeLists.txt` compila ancora i componenti pipe/process-runner e il ramo non-native della factory li seleziona ancora: le exit condition 4 e 5 sono quindi intenzionalmente aperte e la demolition fisica non deve ancora partire.
