# TICKET-VIDEO-PIPELINE-BACKPRESSURE-V1 — Encoder/conversion/pipe backpressure-aware metrics

## Stato: OPEN (implementazione su `main`, macchina-verifica DEFERRED-WBH)

## Problema

La telemetria video non distingue il **lavoro CPU** (submit/conversione/copia)
dall'**attesa di back-pressure** (encoder pieno, pipe non drenata). Oggi:

- `native_av_send_frame_ms` misura solo `avcodec_send_frame`, ma il tempo speso
  nel loop `EAGAIN` (`drain_packets` + retry) è perso — non si sa quanto sia
  davvero back-pressure.
- `native_av_trailer_ms` mescola il flush dell'encoder (`avcodec_send_frame(NULL)`
  + `drain_packets`) con la finalizzazione del mux (`av_write_trailer`).
- `native_av_convert_ms` è una scatola nera: non distingue la quantizzazione
  float→RGBA8 dal color-space conversion RGBA→YUV di `sws_scale`.
- `video_pipe_write_ms` / `ffmpeg_pipe_write_blocked_ms` misurano il wall time
  della scrittura nel pipe, che può contenere l'attesa `poll()`; la sola copia
  `::write()` non è separata.

Conseguenza: quando il render passa da 18 ms/frame a 8 ms/frame, non sappiamo se
il guadagno viene dal graph executor, dalla conversione, dall'encoder o
dall'eliminazione del readback/back-pressure.

## Soluzione

8 nuovi counter nel dominio `CHRONON_COUNTERS_VIDEO` (auto-registrati in
`kCounterNames` + tabella `render_counters` sqlite), con suffissi espliciti
`*_cpu_ms` / `*_wall_ms` / `*_wait_ms`:

| Counter | Significato | Punto di cablaggio |
| ------- | ----------- | ------------------ |
| `encoder_submit_cpu_ms` | `avcodec_send_frame` puro (CPU submit) | `native_av_encoder_write.cpp` |
| `encoder_backpressure_wait_ms` | `drain_packets` durante il loop `EAGAIN` | `native_av_encoder_write.cpp` |
| `encoder_flush_ms` | `avcodec_send_frame(NULL)` + `drain_packets` finali | `native_av_encoder.cpp` `close()` |
| `mux_finalize_ms` | `av_write_trailer` (chiusura container) | `native_av_encoder.cpp` `close()` |
| `pixel_format_convert_ms` | quantizzazione float→RGBA8 (`packed::convert_fb_to_rgba8`) | `packed_backend.cpp` |
| `color_space_convert_ms` | RGBA→YUV via `sws_scale` | `swscale_backend.cpp` `dispatch()` |
| `pipe_write_cpu_ms` | solo `::write()` (copia nel kernel pipe buffer) | `ffmpeg_pipe_sink.cpp` `write_to_pipe` |
| `pipe_write_wall_ms` | wall time di `write_for` (può contenere attesa `poll`) | `ffmpeg_pipe_sink.cpp` `write_to_pipe` |

Note semantiche:

- `encoder_submit_cpu_ms` **+** `encoder_backpressure_wait_ms` = vecchio
  `send_ms`; il drain interno al loop EAGAIN è già contabilizzato dai counter
  receive/mux, quindi lo split non introduce doppio conteggio — ri-etichetta
  soltanto l'attesa che prima veniva persa.
- `pixel_format_convert_ms` è cablato dentro `convert_fb_to_rgba8`, quindi copre
  sia il path packed diretto (RGBA8) sia lo staging del path swscale, senza
  sovrapporsi a `color_space_convert_ms` (solo `sws_scale`).
- `native_av_trailer_ms` ora misura solo `av_write_trailer` (mux finalize),
  allineando il nome al significato; il flush è spostato in `encoder_flush_ms`.
- `pipe_write_wall_ms − pipe_write_cpu_ms` = attesa `poll()`/back-pressure;
  `pipe_write_cpu_ms` è la copia reale.

## Evidenza (file toccati)

- `include/chronon3d/core/profiling/render_counter_macros.hpp` — 8 counter in
  `CHRONON_COUNTERS_VIDEO`.
- `apps/chronon3d_cli/utils/video/native_av_encoder_write.cpp` — split submit vs
  back-pressure nel loop EAGAIN.
- `apps/chronon3d_cli/utils/video/native_av_encoder.cpp` — split flush vs
  mux-finalize in `close()`.
- `src/media/frame_conversion/backends/packed_backend.cpp` — `pixel_format_convert_ms`.
- `src/media/frame_conversion/backends/swscale_backend.cpp` — `color_space_convert_ms`.
- `src/media/video/process_runner.hpp` + `process_runner_posix.cpp` — out-param
  `cpu_write_ms` su `write_for` (default `nullptr`, nessun chiamante esistente rotto).
- `src/media/video/ffmpeg_pipe_sink.cpp` — `pipe_write_cpu_ms` + `pipe_write_wall_ms`.
- `apps/chronon3d_cli/utils/telemetry/telemetry_capture.hpp` — persistenza.
- `include/chronon3d/core/profiling/benchmark_report.hpp` +
  `src/core/benchmark_report.cpp` + `tests/cli/bench_json_tests.cpp` — JSON
  `chronon3d.bench.v3` con roundtrip.

## Criteri di accettazione

- [ ] Gli 8 counter compaiono in `kCounterNames` e sono persistiti in
      `render_counters` via `capture_counters()`.
- [ ] `encoder_submit_cpu_ms + encoder_backpressure_wait_ms` ≈ vecchio
      `native_av_send_frame_ms` su un render con back-pressure (x264 threads=auto).
- [ ] `encoder_flush_ms` e `mux_finalize_ms` sono entrambi ≥ 0 e non si
      sovrappongono: `native_av_trailer_ms` ora è il solo mux finalize.
- [ ] Su un render YUV420P: `pixel_format_convert_ms` > 0 e
      `color_space_convert_ms` > 0, e la loro somma ≈ `native_av_convert_ms`
      (modulo troncamento ms).
- [ ] `pipe_write_cpu_ms ≤ pipe_write_wall_ms`; su un pipe non drenato il gap
      (back-pressure) cresce, su un pipe caldo il gap ≈ 0.
- [ ] Roundtrip bench JSON degli 8 campi verificato da `bench_json_tests`.
- [ ] Nessun nuovo simbolo SDK pubblico; `write_for` mantiene la firma
      compatibile (out-param con default).

## Forward-points

- **TICKET-VIDEO-PIPELINE-BACKPRESSURE-V1-WBH-VERIFY** — build + `ctest` su
  working build host (questo ambiente non dispone di vcpkg FFmpeg/Vulkan); run
  reale `--backend native` + `--backend pipe` per validare che lo split
  submit/back-pressure e cpu/wall resti monotono e che i totali non diverga.
- **`gpu_readback_ms` / `encoder_buffer_copy_ms`** (plan #12) — applicabili solo
  al futuro path GPU→NV12→NVENC senza readback CPU; fuori scope in un path
  conversione CPU.
- **`encoder_device_ms`** (plan #13) — misurabile solo da un backend hardware
  (NVENC/QSV); non stimarlo, resta forward-point.
- **Per-pass timings** (`--timing-level passes`) — già forward-point in
  `TICKET-VULKAN-TIMING-V1-PASSES`; non duplicato qui.
