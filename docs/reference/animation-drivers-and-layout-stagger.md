# Deterministic animation drivers and layout stagger

Render Plan V3 scalar tracks support three deterministic procedural drivers:

- `oscillate` evaluates a sine, triangle, sawtooth, or square wave from frame,
  frequency, phase, amplitude, and offset.
- `random_hold` hashes the seed and integer interval bucket with SplitMix64 to
  produce one stable value per bucket. It does not use a process-wide RNG.
- `posterize_time` quantizes the base track's sample frame to the requested
  frame rate before evaluating the existing keyframes.
- `data_feed` samples an inline, fingerprinted numeric series by composition
  frame, with explicit step or linear interpolation and endpoint hold.

Each track can specify at most one procedural driver. Drivers apply only to
supported scalar properties, are strictly decoded, and participate in the
render-plan content fingerprint. The fingerprint format version is bumped
whenever their canonical representation changes.

Flow and grid layout descriptors can include a `stagger` object. The decoder
collects members by layout kind and group ID, computes deterministic ranks in
layer declaration order, and shifts matching child keyframes by
`rank * per_item_frames` before the plan fingerprint is computed. `center_out`
orders equal-distance items from lower to higher index; `random` uses a local
SplitMix64 Fisher-Yates shuffle seeded by `seed`. Members that do not contain
the target property are left unchanged. All stagger declarations present in
one group must agree. The optional stagger easing replaces the target track's
track-level easing; it cannot replace a spring track.

The V3 extension schema owns these fields. The V2 compatibility projection
removes only V3 additions before validating shared fields against the frozen
V2 schema.

## Following a path

A V3 layer may use `follow_path` with either inline `path_commands` or a
`path_id` naming a layer with inline shape path commands. The scalar `progress`
track uses the normal keyframe timing/easing/spring evaluator. A fixed
subdivision arc-length table is built once during plan compilation and reused
for every frame. `clamp_end: false` extrapolates past either endpoint along its
endpoint tangent, including spring overshoot. `auto_orient` sets Z rotation from
the sampled tangent plus `offset_deg`; explicit layer rotation tracks take
precedence. Position tracks cannot coexist with `follow_path`, since both
would own the same transform channel. The table and all source/progress
parameters are covered by the render-plan fingerprint.

## Animated mask parameters

V3 masks accept `feather_track`, `expansion_track`, and `opacity_track`. Each is
a scalar animation track. Decoder validation limits feather to non-negative
values, expansion to the supported signed distance range, and opacity to
`[0, 1]`. The compiler samples these tracks per frame and includes them in the
plan fingerprint. Opacity scales the resulting mask alpha, including for
inverted masks.

Layer transforms also accept `skew` and `skew_axis` in degrees, including
scalar animation tracks for either component. The compiler applies the shear
in the XY plane around the selected axis. A 2D `position` keyframe may provide
`easing_xy: ["in_quad", "out_quad"]` to select independent x and y easing;
this cannot be combined with per-key Bezier or spring timing overrides.

## Camera shake

V3 accepts an optional root `camera_shake` object alongside `camera`. It
supports bounded amplitude in pixels, frequency, exponential decay, a 32-bit
seed, rotation amplitude, start frame and optional duration. Evaluation uses
quintic-smoothed value noise from a SplitMix64 hash; it depends only on frame,
seed and authored parameters. A zero duration means the shake continues
through the composition. Camera shake requires an authored camera and is
included in the plan fingerprint.

## Effect parameter tracks

V3 layers may declare `effect_param_tracks`, keyed by the canonical catalog
`effect_id` and parameter name. The decoder accepts only parameters listed in
`effect_param_anims.def`, validates the scalar bounds from that table, and
requires the target effect to exist in the layer. Tracks use the same keyframe,
easing, spring, loop and deterministic driver evaluation as scalar layer tracks.
At scene evaluation the compiler samples the parameter for the current frame
and dispatches the resulting typed effect parameters. The effect ID, parameter
and complete track are part of the render-plan fingerprint.

Effects without a dedicated V3 shorthand can be instantiated with
`{"type":"catalog","effect_id":"color.tint","params":[{"param":"amount","value":0.5}]}`.
The generic form accepts only scalar parameters declared in the catalog
animation table and validates each value against that parameter's bounds. LUT
and displacement-map effects continue to use their dedicated forms because
they need external asset or layer inputs.

## Audio reaction

An animation track may use `audio_react` instead of keyframes. `source` points
to an audio-mix clip as `mix:0` or `mix:<clip-id>`. Compilation decodes that
clip to mono 48 kHz, bakes one scalar sample per composition frame, and lowers
the result to an immutable frame evaluator. The render loop performs no audio
I/O. The decoder/analyzer contract, source digest, analysis settings, and baked
samples enter the plan fingerprint. This feature requires a build configured
with `CHRONON3D_ENABLE_NATIVE_FFMPEG`.
