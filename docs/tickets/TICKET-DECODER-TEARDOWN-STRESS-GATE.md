# Native Decoder Teardown Stress Gate

P0 gate for the FFmpeg concurrent decoder teardown heap-corruption fix.

## Symptom

Concurrent create/decode/destroy of `NativeVideoFrameDecoder` sessions surfaced
an intermittent heap corruption:

```
malloc_consolidate(): invalid chunk size
```

## Harness

`tests/video/test_native_decoder_teardown_stress.cpp` — isolated stress test
exercising the decoder across four concurrency topologies plus a bisection
matrix that pinpoints the guilty subsystem.

### Concurrency cases

| Case | Sources | Threads | Iterations | Topology |
|------|---------|---------|------------|----------|
| A    | 1       | 1       | 1000       | single decoder create/decode/destroy |
| B    | 2       | 1       | 1000       | sequential, two sources |
| C    | 2       | 2       | 1000       | concurrent, two decoders |
| D    | 8       | 8       | 1000       | concurrent, eight decoders |

### Bisection matrix (CASE C, 200 iterations)

`NativeDecoderTestOptions` progressively disables one subsystem per row. The
first row that passes where the previous crashed names the corruption source.

| Row | prefetch | swscale | cache | |
|-----|----------|---------|-------|-|
| 1   | ON       | ON      | ON    | production |
| 2   | OFF      | ON      | ON    | |
| 3   | OFF      | OFF     | ON    | |
| 4   | OFF      | OFF     | OFF   | |

## Build & run (ccache + mold, -j20)

```bash
cd Chronon3d
cmake --preset linux-asan-native -B build/chronon/linux-asan-native
cmake --build build/chronon/linux-asan-native \
    --target chronon3d_native_decoder_teardown_tests -j20

ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    ./build/chronon/linux-asan-native/tests/chronon3d_native_decoder_teardown_tests \
        -tc="*teardown*"
```

Or via the test preset:

```bash
ctest --test-dir build/chronon/linux-asan-native \
    -R chronon3d_native_decoder_teardown_tests --output-on-failure
```

## Gate (PASS criteria)

- **CPU**: 1000/1000 concurrent teardown PASS across CASE A-D.
- **ASan**: 0 out-of-bounds, 0 use-after-free.
- **UBSan**: 0 undefined behavior.

The GPU gate (100/100 CUDA/NVDEC teardown) runs on the self-hosted NVIDIA
runner once the CPU gate is green; the same harness switches to NVDEC when
`CHRONON3D_ENABLE_CUDA_INTEROP` is on and a CUDA device is available.

## Ownership contract verified

The `Session::~Session()` destructor releases every object that can retain an
`AVBufferRef` derived from the decoder **before** `avcodec_free_context()` and
`av_buffer_unref(hw_device_ctx)`:

```
prefetch_queue.clear()      → cache.clear()          →
eof_frame.reset()          → captured/eof HwFrameRef →
native_import_session.reset() →
av_frame_free(decoded/closest/hw_transfer/packet) →
sws_freeContext(sws)       →
avcodec_free_context(codec) → av_buffer_unref(hw_device_ctx) →
avformat_close_input(fmt)
```

Any new member that holds an `AVBufferRef` derived from the decoder must be
released in this window or it will run against a dead CUDA device and
re-introduce the corruption.
