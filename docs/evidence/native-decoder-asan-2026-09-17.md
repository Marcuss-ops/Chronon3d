# Native decoder teardown — ASan evidence (2026-09-17)

## Scope

Chiusura del P0 `NativeVideoFrameDecoder` heap corruption nel teardown delle
sessioni indipendenti. La verifica è stata eseguita sull'albero aggiornato a
`c6f4c69d1` (`origin/main`), con i fix locali descritti sotto.

## Cause and fix

`frame_to_framebuffer` dimensionava `rgba_staging` con il solo risultato di
`av_image_get_buffer_size`. Sulla fixture Y4M 2×2, libswscale poteva quindi
scrivere oltre il buffer RGBA packed durante la conversione SIMD; l'heap veniva
poi rilevato corrotto in `sws_freeContext` o `avformat_close_input`, cioè durante
il teardown e non nel punto della scrittura.

Il fix in
`src/media/video/native_video_frame_decoder_helpers.cpp` mantiene `align=32` e
riserva 64 byte aggiuntivi di padding SIMD prima di chiamare `sws_scale`.

## Reproduction and result

Configurazione e build:

```sh
VCPKG_ROOT=/home/pierone/vcpkg cmake --preset linux-asan-native
cmake --build build/chronon/linux-asan-native \
  --target chronon3d_native_decoder_teardown_tests -j8
```

Gate decoder-only con LeakSanitizer attivo:

```sh
ASAN_OPTIONS='halt_on_error=1:abort_on_error=1:detect_leaks=1:quarantine_size_mb=0:thread_local_quarantine_size_kb=0' \
UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
./build/chronon/linux-asan-native/tests/chronon3d_native_decoder_teardown_tests \
  -tc='decoder*'
```

Risultato: **9/9 test case, 222/222 assertion, 0 failure, 0 leak, exit 0**.

La suite completa ha inoltre eseguito 12/12 casi e 2225/2225 assertion; il
processo aggregato non è usato come gate P0 perché LeakSanitizer segnala 106000
byte nella stress lane dell'encoder FFmpeg, separata dal decoder. Il filtro
decoder-only passa con leak detection attiva.
