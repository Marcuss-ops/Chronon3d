# TICKET — Native decoder software (sws) multi-frame materialization corrupts the heap

Status: Closed (aligned staging + same-session regression verified)
Priority: P1 (software-fallback reliability) / low production impact (GPU path unaffected)
Owner: rendering-core / media

## Symptom

On the pinned FFmpeg 4.x (system `libavcodec.so.58`, `libavformat.so.58`),
decoding **two or more software (non-CUDA) frames through one session** and
then destroying the session aborts in `Session::~Session()` with
`malloc: free(): invalid next size (fast)` — heap metadata was corrupted by an
earlier out-of-bounds write.

Reproduces with NO retained-frame/capture involvement:

```
iso C: decode_frame_at(0); decode_frame_at(1);   // same decoder/session
      -> SIGABRT in Session dtor (heap corruption)
iso A: decode_frame_at(1) as the FIRST request (seek-back path on y4m)
      -> SIGABRT in Session dtor
```

The existing native-decoder suite never hit this because it decodes exactly
one frame per session (`test_native_video_frame_decoder.cpp`), so a second
software conversion in the same session was never exercised.

Also observed along the same path: a repeated request that falls through to a
second `avcodec_flush_buffers()` on a y4m source crashes inside FFmpeg
(`av_buffer_unref` ← `av_packet_unref` ← `avcodec_flush_buffers`).

## Suspects

- `frame_to_framebuffer()`/`session.sws` reuse across frames: the second sws
  conversion in a session writes out of bounds for tiny frames (2x2) when the
  sws linesize alignment exceeds the tightly-sized staging buffer.
- `av_seek_frame` + `avcodec_flush_buffers` re-entry on the rawvideo/y4m
  demuxer in FFmpeg 4.x.

## Repro

See the scratch harness used during investigation (session decoding 2x2 y4m,
two sample indices, one decoder instance). A standalone doctest file was kept
out of the suite on purpose; the in-tree retained-replay test
(`tests/video/test_native_video_retained_replay.cpp`) deliberately drives only
the native-capture materialization path (no sws) because of this defect, and
documents this ticket in its scope note.

## Required before closing

- [x] Reproduce the regression under ASan (rebuild media_native + pipeline +
      core with
      `-fsanitize=address`), capture the exact OOB write stack.
- [x] Fix the sws staging/linesize handling in the software materialization
      path with FFmpeg-owned aligned RGBA storage and the returned destination
      stride for every conversion.
- [x] Add the two-sample software decode back to the native decoder test suite
      and verify clean teardown under the regular and focused ASan lanes.
- [x] Confirm the seek/flush re-entry path on y4m: the first seek, forward
      second frame, and retained-window replay all pass without heap damage.
