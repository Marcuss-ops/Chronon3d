# Project Model

This document is the working rulebook for how we structure Chronon3d code.

## Core Rule

- Put behavior in code.
- Put variation in data.
- Put orchestration in the CLI.
- Put build selection in CMake presets.
- Prefer shared utilities that any feature can consume, instead of one-off logic owned by a single clip.

## What Belongs Where

### Code

Use code when the logic is real and worth testing:

- rendering behavior
- camera motion math
- composition evaluation
- encoder implementation details
- resource loading and backend glue

### Data

Use data when only values change:

- camera axis, duration, reference image, and frame range
- render profiles such as preview or final
- video encode options such as codec, preset, and CRF
- asset paths and explicit batch job specs passed on the CLI

### CLI

Use the CLI for user-facing intent:

- `doctor`
- `verify`
- `render`
- `video`
- `video camera`

The CLI should assemble existing pieces. It should not duplicate rendering logic.

### Shared Utilities

Some behavior should be reusable across domains:

- camera motion can be consumed by image clips, text cards, or video exports
- overlays can be attached to any composition that needs them
- particles, frames, shadows, and effects should be shared helpers when possible

This is the "one behavior, many consumers" rule. If a utility is useful in multiple places, promote it to a public contract.

### Build Presets

Use CMake presets for environment selection:

- `win-debug`
- `win-release`
- `win-debug-video`
- `win-release-video`

Build selection should happen once. Rendering should not reconfigure the project.

## Good Pattern

Prefer this shape:

```cpp
CameraMotionParams params;
params.axis = MotionAxis::Tilt;
params.reference_image = "assets/images/camera_reference.jpg";
params.start_frame = 0;
params.duration = 150;
```

Then pass `params` into the composition or command that needs it.

## Bad Pattern

Avoid these:

- three almost identical compositions for one concept
- shell scripts that rebuild before every render
- hardcoded codec fallback that hides what happened
- duplicated flags in several places when one props struct would do

## Rule Of Thumb

If you are changing:

- a value, use data
- behavior, use code
- user intent, use the CLI
- build environment, use presets

If a change forces you to touch many places, ask whether one of those places should become a single source of truth.
