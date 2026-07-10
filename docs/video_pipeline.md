# Video Export Pipeline

> Stato: Giugno 2026 — Pipeline riscritta con VideoSink unificato.
> Tutti i fix principali sono su `main`.

## Call Chain

```
CLI: chronon3d_cli video <composition> -o output.mp4
  │
  └─► command_video.cpp
        └─► create_video_encoder()          ─── video_export_common.cpp
              │                                   (factory: sceglie encoder in base al sink type)
              │
              ├─► VideoSinkEncoderAdapter    ─── video_sink_adapter.cpp
              │     (bridge IVideoEncoder → VideoSink)
              │     │
              │     ├─► build_sink_config()  → VideoSinkConfig
              │     │     ├─ resolve_auto_codec()  ─── video_config.hpp
              │     │     ├─ map_codec()
              │     │     └─ detect_container()
              │     │
              │     ├─► FrameConversionService::convert_to_buffer()
              │     │     └─► direct_yuv_converter_hwy.cpp  (Highway SIMD)
              │     │
              │     └─► sink_->submit(view)  → VideoSink::submit()
              │           │
              │           └─► write_to_pipe()
              │                 └─► ProcessRunner::write_for()  ─── process_runner.cpp
              │                       (poll-based, con deadline + stderr drain)
              │
              └─► NullRenderEncoder         ─── video_sink_encoders.cpp
              └─► NullConvertEncoder        ─── video_sink_encoders.cpp
```

### Output sink types

```
VideoSinkEncoderAdapter(VideoSinkType::Ffmpeg)
  └─► create_video_sink(config)
        ├─► FfmpegPipeSink                 ─── ffmpeg_pipe_sink.cpp
        │     ├─► ProcessRunner::launch()   (posix_spawnp, no shell)
        │     ├─► ProcessRunner::write_for() (poll POLLOUT + stderr POLLIN)
        │     └─► ProcessRunner::wait_for()  (poll WNOHANG + stderr drain)
        │
        └─► RawVideoSink                   ─── raw_video_sink.cpp
              (scrive pixel raw su file, nessun encoding)
```

## Architettura VideoSink

```
VideoSink (interfaccia astratta)          ─── include/chronon3d/media/video/video_sink.hpp
  ├── FfmpegPipeSink                      ─── src/media/video/ffmpeg_pipe_sink.cpp
  │     pipe raw frame → FFmpeg subprocess
  │     codec: H264/H265/VP9/AV1 (con risoluzione Auto per container)
  │     timeout: configurabile (default 30s per frame write)
  │     stderr: catturato con cap 1 MB (conserva ultimi dati)
  │
  └── RawVideoSink                        ─── src/media/video/raw_video_sink.cpp
        pixel raw → file .raw/.yuv
        nessuna dipendenza da FFmpeg
```

### ProcessRunner

```
ProcessRunner                             ─── process_runner.hpp/.cpp
  launch()   → posix_spawnp (no shell)
  write()    → fd write con EINTR retry
  write_for()→ poll(POLLOUT) con deadline + stderr drain simultaneo
  wait_for() → waitpid WNOHANG con poll loop + stderr drain
  close_stdin() → EOF al child
  terminate_and_wait() → SIGTERM → wait → SIGKILL → reap
  consume_stderr() → diagnostica

  O_NONBLOCK su stdin + stderr
  Pipe buffer: 2 MB
  Stderr cap: 1 MB (ultimi dati, erase front)
  Timeout 0: tratta come 24h (write_for sempre usata)
```

### Configurazione

```
VideoSinkConfig                           ─── video_config.hpp
  ├── VideoStreamConfig     (width, height, fps, submitted_format)
  ├── VideoEncoderConfig    (codec, crf, preset, tune, encoded_pixel_format)
  ├── PipeTransportConfig   (queue_capacity, asynchronous, write_timeout)
  └── VideoOutputConfig     (output_path, container, overwrite)

  default: MP4, H264, YUV420P, CRF=-1, async=false, timeout=30s
```

### Validazione centrale

`validate_video_sink_config()` è il punto di verifica unico per tutte le configurazioni.
Chiamato da ogni sink in `open()`.
Controlla: dimensioni, YUV pari, CRF range, codec/container compatibilità, path, transport.
Risolve Auto codec prima della validazione.

## File chiave

| File | Ruolo |
|------|-------|
| `apps/chronon3d_cli/utils/video/video_sink_adapter.cpp` | Bridge CLI → VideoSink |
| `apps/chronon3d_cli/utils/video/video_sink_encoders.cpp` | NullRender, NullConvert, RawVideoSinkEncoder |
| `src/media/video/ffmpeg_pipe_sink.cpp` | Sink FFmpeg pipe (produzione) |
| `src/media/video/process_runner.cpp` | Subprocess no-shell (Linux: posix_spawnp) |
| `src/media/video/raw_video_sink.cpp` | Sink raw file (test/reference) |
| `src/media/video/video_sink_factory.cpp` | Factory create_video_sink() |
| `src/media/video/video_config_validator.cpp` | Validazione centralizzata |
| `include/chronon3d/media/video/video_config.hpp` | Config struct + resolve_auto_codec() |
| `src/media/frame_conversion/frame_conversion_service.cpp` | Conversione RGB→YUV/etc |
| `src/media/frame_conversion/direct_yuv_converter_hwy.cpp` | Highway SIMD YUV |  <!-- drift-allow: stale-ref -->

## Tests

| Target | Cosa testa |
|--------|------------|
| `chronon3d_media_video_tests` | Sink, conversione, adapter E2E (134 test) |
| `chronon3d_cli_tests` | CLI encoder, export helpers |
| `test_ffmpeg_pipe_sink.cpp` | FfmpegPipeSink unitario |
| `test_raw_video_sink.cpp` | RawVideoSink unitario |
| `test_video_adapter_e2e.cpp` | Adapter E2E raw + ffmpeg + ffprobe |
| `test_frame_converter.cpp` | Frame conversion |

## Stato deploy (Linux CPU-only)

| Area | Stato |
|------|-------|
| CLI → adapter → VideoSink | ✅ 10/10 |
| Timeout scritture | ✅ 9/10 |
| Drain stderr | ✅ 9/10 |
| PID e telemetry | ✅ 9/10 |
| Test end-to-end | ✅ 9/10 |
| Auto codec resolution | ✅ 9/10 |
| Failed state preservation | ✅ 9/10 |
| Stderr buffer tail | ✅ 9/10 |
| CLI tests linking | ✅ 10/10 |
| Export reale verificato | ✅ (AnimFadeInText, 60f, 1920×1080, h264, yuv420p) |
