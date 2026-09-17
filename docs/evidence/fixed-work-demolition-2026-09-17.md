# Fixed-work demolition census — 2026-09-17

## Z4 — peak memory polling

Closed. `TelemetryManager` no longer owns a periodic sampler thread, atomic
stop flag, one-second sleep, or destructor join. Linux reads `VmHWM` from
`/proc/self/status` on demand and falls back to `getrusage(RUSAGE_SELF)`;
the API is intended for telemetry snapshots, not a per-pixel path.

Evidence:

```text
cmake --build build/chronon/linux-asan-native --target chronon3d_core_tests -j10  PASS
chronon3d_core_tests -tc='*peak*'                                      1/1, 3/3
```

## Z1 — animated text tracking

Closed at the shader-ABI level. `tracking_prefix()` was an `O(pixel × k × n)`
scan inside `text_tile_raster.comp`. The host now prepares one dynamic record
per glyph per frame:

```text
selector_weight(glyph)       O(n)
tracking_prefix(glyph)       O(n)
pixel shader lookup          O(1)
```

The dynamic record is separate from `VulkanGpuGlyphStatic`, because selector
and animator values change with the sampled frame. Vulkan binding 6 carries
the new 8-byte `GpuGlyphDynamic` array; the static glyph ABI remains 48 bytes.
The binning shader remains unchanged because it never consumed selector
weights; invisible contributors are still filtered in raster exactly as
before. The canary mirrors the new descriptor and buffer contract.

Evidence:

```text
chronon3d_shader_abi_consistency_tests  12/12, 89/89
chronon3d_text_tile_raster_canary_tests 1/1, 83/83
```

## Z2 — compiled gradients

The canonical graphics model now exposes `CompiledGradient` and
`compile_gradient()`. Authoring stops remain sRGB; compilation converts them
once to linear, and `sample_compiled_gradient()` contains no color-space
conversion. The old `sample_gradient(GradientDefinition, ...)` compatibility
API remains available for callers that have not yet adopted a compiled
instance.

The software renderer's custom path now compiles the legacy fill at draw
setup and samples the compiled record in the pixel loop. Regression evidence:

```text
chronon3d_deterministic_tests -tc='*lowered once*'  1/1, 8/8
chronon3d_renderer_core_tests -tc='*gradient*'       27/27, 62/62
```

The scene-layer `GradientFill` is still a compatibility model with live
callers in the legacy path rasterizer and stroke helpers. It is not deleted
until those callers migrate to the canonical model; deleting it now would
create a second, unsafe migration rather than demolition.

## Z3 — LruCache

No code change required. `timed_start()` returns an empty time point when
detailed timing is disabled and `add_ns_if_enabled()` returns before reading
the clock. The census found `stats()` callers in telemetry/diagnostic paths,
not the normal per-frame lookup path. `stats()` remains an explicitly
serialized snapshot operation.

## Z5 — dependencies

- FlatBuffers and zstd are used by the mandatory compiled-artifact cache, not
  only IPC. FlatBuffers is now listed in the public dependency registry for
  that reason; IPC reuses the same dependency.
- `magic_enum` is a public-header dependency through `enum_utils.hpp`; its
  replacement remains deferred until SDK compile-time measurement exists.
- GLM remains the implementation authority behind the public `Vec2/Vec3/Mat4`
  aliases and is not removed.
- ICU remains required by the text feature because it owns grapheme, word,
  line-break, and Unicode property semantics. Optionalization is a product
  capability change, not a cleanup.
