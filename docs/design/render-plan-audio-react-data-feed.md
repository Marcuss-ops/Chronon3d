# Render Plan audio-react and data-feed design

Status: reviewed design. Inline and digest-bound asset `data_feed` V1 are
implemented after WP1–WP3 closed. `audio_react` still needs its offline analysis
implementation; its determinism contract and analysis format are specified
below.

## Deterministic contract

The compiled render plan must be self-contained. A render may read only the
resolved plan and immutable assets whose digest is present in its manifest.
It must not read an audio device, wall clock, process-global random generator,
network endpoint, mutable file, or application callback while rendering.
Changing any analysis input or baked sample must change the content fingerprint.

## Audio analysis and `audio_react`

`audio_react` is an authoring instruction resolved before render-plan
fingerprinting. Its `source` is `mix:<index>` or `mix:<clip-id>` and identifies
an audio clip in `AudioMixPlan`; analysis uses the exact source bytes and the
clip's trim, start, mute, static gain, animated gain, and mix master gain. The initial implementation requires a build with
`CHRONON3D_ENABLE_NATIVE_FFMPEG`, uses FFmpeg to decode the source to mono float
at 48 kHz, and fails compilation if the source cannot be decoded or exceeds
120 million decoded samples. Compilation also caps the analysis work at two
billion window/bin operations so long compositions fail before an unbounded
compile-time scan.

The compiler produces one feature value per composition frame. V1 supports
`feature: "rms"`; without `band_hz` it is broadband RMS over a centered 2048
sample window. With `band_hz: [low, high]`, it uses 32 logarithmically spaced
Goertzel bins inside the requested band, a Hann window, and RMS normalization.
Band edges must be below the 24 kHz Nyquist frequency of the 48 kHz analysis
stream. Smoothing is a causal moving average of `smoothing_frames` values, so
frame N never depends on a future frame. Per-frame clip gain and mix master
gain are applied before smoothing; muted source clips bake as silence. The
track `gain` is applied to the baked values; V1 `map` is `linear`.

The track stores the source clip reference, feature kind, band limits where
relevant, smoothing window, gain, mapping mode, analyzer version, source SHA-256,
and baked sample sequence. The fingerprint includes every field and every
sample in a canonical numeric encoding. Missing clip IDs, unsupported feature
names, invalid bands, unsupported decoder builds, or a table with the wrong
frame count fail compilation. They never fall back to zero or live audio.

## `data_feed`

V1 accepts inline numeric `values` or a logical JSON asset reference with
`asset`, lowercase `sha256`, and `item_count`. The asset must contain a JSON
array of exactly that many finite numeric samples and is limited to 32 MiB.
Compilation resolves the asset under the mounted root, verifies SHA-256 and
item count, then embeds the immutable values in the prepared plan. Sampling uses composition frame plus
`index_offset`, with explicit `step` or `linear` interpolation. Out-of-range
reads hold the nearest endpoint. The schema bounds the series length and the
strict decoder rejects non-finite values and unknown fields. A later asset
reference is fingerprinted by its logical path and content digest, so cache
identity is independent of filesystem location or parse timing. Inline values
are validated by the decoder; asset values are validated during compilation.

## Pipeline and rollout

The schema introduces authoring objects. The decoder rejects malformed and
unknown fields; compilation resolves external data under the mounted asset
root. The compiler lowers sampling into the standard track evaluation path.
The fingerprint hashes the source digest, analysis contract, resolved values,
and interpolation/mapping parameters. Golden tests must compare baked tables
and rendered frame hashes across repeated renders.

WP1–WP3 are closed. `data_feed` inline and asset forms are implemented.
`audio_react` schema, strict decoder, fingerprint fields, and compilation-time
FFmpeg decode/bake path are implemented; end-to-end bake tests must run on the
native-FFmpeg build lane.
