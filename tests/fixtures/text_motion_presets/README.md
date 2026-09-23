# Text motion presets

Eight After Effects style motion studies built from Chronon's existing render-plan selectors and animator properties. They cover grapheme and word focus reveals, reverse order, deterministic random order, vertical unfold with overshoot, focus/zoom, a three-beat blur crossfade and a forward typewriter entrance followed by a right-to-left erase. A generated blue studio stage and one supported Gaussian text halo provide a consistent optical setting.

Generate and render with:

```bash
python3 Chronon3d/tests/fixtures/text_motion_presets/generate.py
bash Chronon3d/tests/fixtures/text_motion_presets/run.sh
```

The runner validates each plan and exports 90 frames at 960×540, 30 fps to `ChrononTemplate/out/text_motion_presets_v1/`. The generator uses Pillow to create the stage image. Chronon's current render-plan contract exposes one Gaussian halo; legacy core/aura/bloom fields are ignored, so the samples do not claim a multi-scale bloom implementation.
