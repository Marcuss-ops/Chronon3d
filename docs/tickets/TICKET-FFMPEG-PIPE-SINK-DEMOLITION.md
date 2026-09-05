# TICKET-FFMPEG-PIPE-SINK-DEMOLITION — delete subprocess encoder fallback

**Stato:** OPEN (residual scope redefined 2026-09-04 after negative census)  
**Priorità:** P1 demolition debt  
**Authority:** `src/media/video/video_sink_factory.cpp` + `src/media/CMakeLists.txt`

## 2026-09-04 negative census (authoritative)

| Target | Stato | Evidenza |
|---|---|---|
| `src/media/video/ffmpeg_pipe_sink.*`, `ffmpeg_pipe_args.cpp`, `ffmpeg_frame_submit.cpp`, `process_runner.*` | **GIÀ DEMOLITI** | 0 file in `src/`, `apps/`, `include/` |
| `FfmpegPipeSink` symbol in production code | **ESTINTO** | solo 2 commenti storici in `include/` (aggiornati) |
| `ProcessRunner` | **ESTINTO** | 0 match in src/apps/include/tests |
| `bind_unplanned` (Vulkan surface authority) | **VIVO — non eliminare ora** | `vulkan_surface_authority.hpp` (4 ref); entry point: `vulkan_surface_store_private.cpp:65 ensure()`. Richiede Wave 1 sync authority, non demolition stop-and-pop |
| **Residuo reale: pipe ENCODER lato CLI** | **VIVO** | `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp` (interfaccia `IVideoEncoder`, legittima) + `VideoSinkEncoderAdapter(VideoSinkType::Ffmpeg)` usato quando `encoder_backend != "native"` oppure quando native è richiesto ma non compilato (**downgrade silenzioso** a `video_export_common.cpp:38-41`) |

## Nuovo scope residuo (sostituisce la lista file sopra)

1. **Downgrade silenzioso nativo→pipe**: `create_video_encoder()` con `CHRONON3D_ENABLE_NATIVE_FFMPEG` spento e `encoder_backend == "native"` oggi logga un warn e prosegue in pipe. Deve diventare fail-closed (errore, no output) — stesso contratto P0.1/P0.2 del resto della repo.
2. **`--encoder-backend pipe` come percorso esplicito**: resta consentito come modalità diagnostic/debug dichiarata (null/adaptive), ma mai come fallback silenzioso del native.
3. Cleanup finale di `ffmpeg-mode pipe` doc/options quando il punto 1 è chiiuso.

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
2. [ ] **SDK consumers certificati.** I consumer supportati che producono video compresso sono verificati sul percorso `NativeAvSink`; nessun consumer supportato richiede il subprocess fallback.
3. [ ] **Contratti codec/routing verdi.** Test H264, H265/HEVC e routing raw/uncompressed sono verdi sul build native, inclusi open/submit/flush/close e teardown. La coverage richiesta è ora presente (`029547d2222d7a7730dc1d08c09f4bcf9e0ceed5`, `908de2eaf9159db3b206c1f8d242ad8bfaa5bd1e`, `2fb01f97dcd326714615bf2c3907e93860b35d3c`) e la lane dedicata è `Native Video Certification` (`16f3ffaf36f5b1b946fd69ad263d8ea2347656cf`); resta da acquisire evidenza CI green.
4. [ ] **Production presets a zero pipe caller.** I preset di produzione non selezionano `FfmpegPipeSink`; telemetry/contract runs non mostrano `video_pipe_fallback_frames` nel percorso canonico.
5. [ ] **Caller census finale = zero.** Nessun target di produzione o SDK supportato include/istanzia direttamente `FfmpegPipeSink`, `ProcessRunner` o pipe args.

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

## Evidenza corrente

- `VideoSinkFactory` seleziona `NativeAvSink` nei build con `CHRONON3D_ENABLE_NATIVE_FFMPEG`.
- `linux-video-release` eredita `CHRONON3D_ENABLE_NATIVE_FFMPEG=ON` e, da `36c013b`, la configurazione native fallisce immediatamente se header o librerie libav richieste non sono disponibili.
- `tests/video/test_native_av_sink_factory.cpp` blocca esplicitamente H264 e H265 su `NativeAvSink` e mantiene raw su `RawVideoSink`.
- `tests/video/test_native_av_sink_lifecycle.cpp` esercita H264 e H265 attraverso `open -> submit -> flush -> close` e teardown del sink reale.
- `.github/workflows/native-video.yml` costruisce ed esegue il target native dedicato su Linux; l'evidenza green deve essere registrata prima di marcare l'exit condition 3 come completata.
- `src/media/CMakeLists.txt` compila ancora i componenti pipe/process-runner e il ramo non-native della factory li seleziona ancora: le exit condition 4 e 5 sono quindi intenzionalmente aperte e la demolition fisica non deve ancora partire.
