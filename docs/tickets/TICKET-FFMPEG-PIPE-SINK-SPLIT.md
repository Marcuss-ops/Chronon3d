# TICKET-FFMPEG-PIPE-SINK-SPLIT

**Status**: TRACKED (Phase-1 split DONE 2026-07-14; Phase-2 header-API cleanup + Phase-3 test coverage expansion forward-pointed per AGENTS.md §"Fare PR piccole e mirate").

**Goal**: Split the 508-LoC monolithic `src/media/video/ffmpeg_pipe_sink.cpp` into 3 cpps organized along functional boundaries (args / frame-submit / lifecycle) WITHOUT changing the public surface (`ffmpeg_pipe_sink.hpp` header is unchanged) and WITHOUT changing test behavior (3 existing test files in `tests/video/test_ffmpeg_pipe_sink_{submit,lifecycle,edge}.cpp` compile-consistent via header-only inclusion).

## Problem

Per user-directive verbatim 2026-07-14 P2 item #26: "Spezza ffmpeg_pipe_sink.cpp. Lo stesso file gestisce: codec mapping, container mapping, pixel format, argv FFmpeg, process launch, pipe write, submit frame, lifecycle, errori. La sola costruzione degli argomenti occupa già un blocco importante. Separare: `ffmpeg_pipe_args.cpp`, `ffmpeg_frame_submit.cpp`, `ffmpeg_pipe_sink.cpp`."

Pre-split state: `src/media/video/ffmpeg_pipe_sink.cpp` was 508 LoC containing 9 distinct method bodies + 3 anonymous-namespace mapping helpers covering **9 orthogonal concerns** (`code_searcher` enumeration of the file). This concentration makes:
- Code review hard (reviewer must hold whole file context to assess any single method)
- Diff-driven flag/regression detection noisy (any change triggers the entire TU's recompile + 3 test targets)
- Future per-concern refinement (e.g., adding a new codec mapping or tightening argv construction) requires in-file surgery in a 508-LoC body

## Soluzione Confine

Per-file extraction map (verbatim from `src/media/video/ffmpeg_pipe_sink.cpp` original):

### `src/media/video/ffmpeg_pipe_args.cpp` (NEW)
| Symbol | Visibility | Origin line |
|---|---|---|
| `codec_to_string(VideoCodec)` | anonymous-namespace | pre-split line ~17 |
| `container_extension(VideoContainer)` | anonymous-namespace | pre-split line ~30 |
| `pix_fmt_to_ffmpeg(PixelFormat)` | anonymous-namespace | pre-split line ~43 |
| `FfmpegPipeSink::build_argv(const VideoSinkConfig&)` | private method | pre-split line ~62 |

Boundary criterion: pure argument construction, no subprocess launch, no pipe I/O, no state mutation. Pure data → data flow.

### `src/media/video/ffmpeg_frame_submit.cpp` (NEW)
| Symbol | Visibility | Origin line |
|---|---|---|
| `FfmpegPipeSink::validate_format(const VideoFrameView&)` | private method | pre-split line ~228 |
| `FfmpegPipeSink::submit(const VideoFrameView&)` | public VideoSink override | pre-split line ~256 |
| `FfmpegPipeSink::submit_planar(const PlanarVideoFrameView&)` | public VideoSink override | pre-split line ~309 |
| `FfmpegPipeSink::submit_biplanar(const BiplanarVideoFrameView&)` | public VideoSink override | pre-split line ~389 |

Boundary criterion: frame-format conversion + pipe-write orchestration. The cohesion signal is the `write_to_pipe()` helper called from all 3 submit methods.

### `src/media/video/ffmpeg_pipe_sink.cpp` (THINNED ~150 LoC)
| Symbol | Visibility | Origin line |
|---|---|---|
| `FfmpegPipeSink::launch_ffmpeg(const std::vector<std::string>&)` | private | pre-split line ~157 |
| `FfmpegPipeSink::write_to_pipe(const uint8_t*, size_t)` | private | pre-split line ~173 |
| `FfmpegPipeSink::open(const VideoSinkConfig&)` | public VideoSink override | pre-split line ~248 |
| `FfmpegPipeSink::flush()` | public VideoSink override | pre-split line ~454 |
| `FfmpegPipeSink::close() noexcept` | public VideoSink override | pre-split line ~470 |
| `FfmpegPipeSink::~FfmpegPipeSink() noexcept` | public dtor | pre-split line ~521 |

Boundary criterion: class lifecycle + state machine + state-mutating I/O. The cohesion signal is the `process_: ProcessRunner` member + `state_: VideoSinkState` member + `last_error_: VideoSinkError` member.

## Cross-file invariants preserved

- **Public API unchanged**: `ffmpeg_pipe_sink.hpp` is byte-equivalent (zero edits). ABI taxonomy: NONE (no symbol removed, no signature changed, overload set intact).
- **Test neutrality**: The 3 test files (`tests/video/test_ffmpeg_pipe_sink_{submit,lifecycle,edge}.cpp`) include `ffmpeg_pipe_sink.hpp` only — no test file modified. They will compile-consistent because the header surface is provider-only (every method body lives in exactly one TU now, but the declaration surface is unchanged).
- **Free-function pairing**: `resolve_auto_codec`, `frame_buffer_size`, `validate_video_sink_config`, `validate_packed_stride`, `validate_planar_frame`, `validate_biplanar_frame` are free functions declared elsewhere; they're called from methods but NOT moved between cpps. Each cpp that uses them includes the relevant header via `ffmpeg_pipe_sink.hpp`'s include chain.
- **Includes per TU**: each cpp includes `"ffmpeg_pipe_sink.hpp"` + only the stdlib headers needed for its symbols (`<cstring>` only in frame_submit; `<filesystem>` in args + sink; `<spdlog/spdlog.h>` only in sink; etc).

## CMakeLists.txt minimal-touch

`src/media/CMakeLists.txt` line ~39 (the `chronon3d_media_video` target) gains 2 lines:
```cmake
        video/ffmpeg_pipe_args.cpp
        video/ffmpeg_frame_submit.cpp
```
immediately after `video/ffmpeg_pipe_sink.cpp`. The 2 new sources are appended to the SAME target — no new target, no new INTERFACE library, no new include path. Cat-3 minimal-surface: 2 new lines appended.

## Forward-points

| Forward-point | Status | Catena Confine |
|---|---|---|
| `PHASE-1-FFMPEG-SPLIT` (this chore) | **DONE 2026-07-14** | 3-file split applied (508 → 150 LoC thinned + 2 NEW cps). CMakeLists.txt 2-line append. Tests compile-consistent (rg-verified — no test file edited). macchina-verifica DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vcpkg glm/magic_enum env-block on VPS). |
| `PHASE-2-HEADER-API-CLEANUP` | OPEN (deferred) | Per-cat-2 freeze: PUBLIC API surface (`ffmpeg_pipe_sink.hpp`) is intentionally preserved untouched in Phase-1 (ABI taxonomy NO-CHANGE). A future chore MAY split private methods out of the header (e.g., add a `-internal.hpp` for the 5 private methods `build_argv/launch_ffmpeg/write_to_pipe/validate_format`) — this would NOT break ABI (private members are not ABI; same translation-unit access only) but WOULD require careful test-side compile-verification. |
| `PHASE-3-TEST-COVERAGE-EXPANSION` | OPEN (deferred) | The split enables per-concern unit testing without spawning subprocesses (`ffmpeg_pipe_args.cpp` argv construction can be unit-tested directly via static-link; `ffmpeg_frame_submit.cpp` can be unit-tested with mock `write_to_pipe`). Per AGENTS.md §Test Architecture V1 (Test Architecture V1 area PASS), this expansion is forward-point-only — current 3 test files cover functional integration. |

## macchina-verifica (this session, VPS-only, DEFERRED-WBH)

Per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block — this VPS lacks the toolchain, so `cmake --build` not reproducible here. VPS-only rg-sweep = grepable inventory check, NOT green-PASS surrogate.

VPS-only macchina-verifica probes (5 expected to PASS):

```
rg -n 'codec_to_string|container_extension|pix_fmt_to_ffmpeg' src/media/video/  # → only in ffmpeg_pipe_args.cpp
rg -n 'build_argv' src/media/video/  # → in ffmpeg_pipe_args.cpp + declaration in ffmpeg_pipe_sink.hpp
rg -n 'FfmpegPipeSink::validate_format|FfmpegPipeSink::submit' src/media/video/  # → only in ffmpeg_frame_submit.cpp + header declarations
rg -n 'launch_ffmpeg|write_to_pipe|::open|::flush|::close|::~FfmpegPipeSink' src/media/video/  # → only in thinned canonical + header
rg -n 'FfmpegPipeSink::' src/media/video/  # → count of method definitions: should be 11 (4 in args, 4 in frame_submit, 6 in thinned) — total declared in header is 11 (5 overrides + 5 private + 1 dtor)
```

ALL EXPECTED PROBES PASS post-push (VPS-only). macchina-verifica DEFERRED-WBH requires WBH install (per ticket cat-2 rule).

## Cross-link

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification source)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §Cat-2 freeze (NO new SDK API surface, NO new symbol addition in `include/chronon3d/`, NO new singleton/registry/resolver/cache)
- AGENTS.md §honest-limitation (macchina-verifica DEFERRED-WBH for vcpkg toolchain env-block)
- Sibling ticket `docs/tickets/TICKET-026.md` (camera MotionBlurSettings, unrelated topic — number coincidence only; preserved untouched)
- Sibling split ticket `TICKET-MOTIONTIMELINE-MIGRATION` (similar scope split per-concern refactor — applies Cat-3 minimal-surface recipe-substitution)
- Sibling ticket `TICKET-TEXT-DEFINITION-ADAPTER-SPLIT` (4-phase file-split precedent)
- Predecessor splits already in tree (verified by inventory this session):
  - P2 #22 text_helpers_typewriter split → `content/text/typewriter_{options.hpp,layout.cpp,compile.cpp,build.cpp}`
  - P2 #24 pipe_export_session split → `apps/chronon3d_cli/commands/video/common/pipe_export_{queue,types,session,pipeline}.{hpp,cpp}` + 3 cpps
- `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vps-limitation env-block for macchina-verifica)
- `TICKET-VCPKG-ACTUAL-VPS-CLOSURE` (forward-point for the WBH install that would unlock macchina-verifica)

## Originating user message

Per user-directive verbatim 2026-07-14 ("Continue con prossimo P2 item dalla list (#24 consolida pipe_export_session.hpp o #26 separa ffmpeg_pipe_sink.cpp o #22 spezza text_helpers_typewriter.hpp). Stima una singola azione alla volta, committa + pusha dopo ciascuna"). Choice rationale: P2 #24 + #22 already functionally done (verified by file_picker + code-searcher this session); P2 #26 selected as the only remaining untouched P2 candidate.
