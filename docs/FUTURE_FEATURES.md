# Chronon3d — Roadmap & AE Comparison

> Last updated: 2026-05-17 — 272/272 tests passing

---

## Cosa funziona adesso ✅

| Feature | Note |
|---|---|
| Modular render graph (DAG + caching) | ✅ completo |
| 2.5D camera — zoom, FOV, pan, tilt, roll, orbit, POI | ✅ completo |
| Camera motion preset library | ✅ dolly, pan, orbit, tilt_roll, push_in_tilt, parallax_sweep, dramatic_push, roll_reveal |
| Shape types: Rect, RoundedRect, Circle, Line | ✅ |
| Shape types: Text (StbFontBackend) | ✅ — richiede `renderer.set_font_backend(make_shared<StbFontBackend>())` |
| Shape types: Image (StbImageBackend) | ✅ — richiede `renderer.set_image_backend(make_shared<StbImageBackend>())` |
| Shape types: FakeBox3D, GridPlane, FakeExtrudedText | ✅ proiezione nativa 3D |
| Effects: Blur, Tint, Brightness, Contrast, DropShadow, Glow | ✅ |
| Blend modes | ✅ Normal + altri |
| Masks | ✅ funzionanti in modular graph |
| SSAA | ✅ |
| Motion blur (subframe accumulation) | ✅ implementato in software_frame_pipeline |
| Layer parenting (hierarchy, null layer) | ✅ |
| Precompositions | ✅ |
| Video layers (FFmpeg) | ✅ |
| Adjustment layers | ✅ |
| CLI renderer | ✅ |
| Layer rotation 3D (pipeline) | ✅ — proj * view * TRS via view matrix path; non testato visivamente |

---

## Cosa manca per essere "mini AE"

### 🔴 BLOCCANTI

| Feature | Perché manca | Effort |
|---|---|---|
| **Gradient fill** | ShapeType solo solid color | ~2h — nuovo GradientShape + rasterizer |
| **Trim path / animated stroke** | Nessun sistema di stroke animabile | ~4h — StrokeParams + trim_start/end |
| **Layer rotation 3D — test visivo** | Pipeline implementata, mai verificata con `.rotate()` reale | ~30min |

### 🟡 IMPORTANTI

| Feature | Note | Effort |
|---|---|---|
| **Keyframe curves** | Lambda `f(ctx.frame)` funziona, manca API dichiarativa con easing | ~1 giorno — `AnimatedValue<T>` + builder |
| **Track matte** | Layer A come alpha mask per layer B | ~3h — `TrackMatteNode` |
| **Full 3D parenting** | 2D parenting ok; 3D propaga solo traslazione, non rotation/scale | ~2h |
| **Depth of Field** | Struct presente, renderer non implementato | ~3h — post-process pass |
| **Lights (ambient + directional)** | `LightContext` presente, nessuna pipeline | ~4h |
| **Shadows layer-to-layer** | Dipende da lights | dopo lights |

### 🟢 NICE TO HAVE

| Feature | Note |
|---|---|
| Expression system (wiggle, loopOut) | Lua o mini-DSL su `AnimatedValue<T>` |
| Spring physics | `SpringAnimation` struct presente |
| Particle system | Emitter + lifetime + color over life |
| SVG import | parse path → Chronon3d shapes |
| Lottie import | parse JSON → `Composition` |
| Video come 3D card | FFmpeg pipeline ok, manca connessione al projected card path |
| Parallel multi-frame render | Taskflow già linkato, rendering ancora seriale |
| EXR output | vcpkg openexr |
| Preview window interattiva | SDL2/GLFW + hot reload |
| GPU backend (Vulkan) | architettura future |
| WASM / browser | dipende da GPU backend |
| Python bindings | pybind11 wrapper |

---

## Bug risolti ✅

| Bug | Fix |
|---|---|
| Text mirrored con POI camera | `proj[0][0] = -focal` per POI path in `camera_2_5d_projection.hpp` |
| FakeBox3D blank in 2.5D pipeline | identity `projection_matrix` per native-3D layers (`is_native_3d_layer`) |
| SourceNode 3D: shape centrata sbagliata | ripristinato `canvas_center * ssaa_scale * shape_local.to_mat4()` |
| hash_value float collision (cache stale) | `hash_bytes(&member, sizeof(f32))` diretto — evita copia locale con register allocation bug |
| Masks in modular graph | ✅ risolto |
| TransformNode con opacity | ✅ risolto |
| Precomposizione non composita correttamente | ✅ risolto |
| EffectInstance std::any vs std::variant | ✅ risolto |
| Test helpers duplicati in 3 file | ✅ estratti in `tests/helpers/` |
| WHOLE_ARCHIVE linking + ODR violation CLI | ✅ risolto |

---

## Progression verso AE

```
Adesso
  ✅ shape types completi + effetti + 2.5D camera + parenting + precomp

+ gradient fills + trim path + test rotazione
  → usabile per produzione reale (motion graphics di base)

+ keyframe curves + track matte + DOF + motion blur 3D
  → AE Classic 3D equivalent

+ lights + shadows + expressions + particles
  → renderer professionale headless

+ GPU backend + hot reload + Python bindings
  → piattaforma
```
