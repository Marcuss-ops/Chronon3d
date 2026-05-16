# Unified Orchestration

This document states the main design rule for Chronon3d:

> any capability can be consumed by any other capability, as long as the contract is explicit.

The goal is to keep the system fluid:

- images can use camera motion
- text can use the same animation language
- overlays can be reused on camera clips, text cards, video compositions, or scene compositions
- particles, shadows, frames, masks, and effects are shared utilities, not one-off pipelines

Nothing should be "for one thing only" if it can be expressed as a reusable contract.

## Core Idea

Chronon3d should behave like a connected system, not a set of isolated silos.

That means:

- behavior lives in reusable code
- variation lives in parameters
- orchestration lives in adapters and registries
- composition is allowed to cross domains when the contract is stable

Example:

- a camera motion helper can be consumed by an image card
- the same motion can be reused by a title card
- a particles overlay can sit above either one
- the CLI should only select and wire those pieces together

## What "Everything Can Call Everything" Means

This does not mean unrestricted coupling.

It means:

- any domain can reuse another domain's utility through a public contract
- any scene type can opt into the same animation or overlay model
- no feature should be hardwired to a single clip type if it can be generalized

Good:

- `image` reuses a `camera`-style motion preset
- `text` reuses the same overlay and timing rules
- `video` reuses the same effect stack and layer utilities
- `scene` reuses common builders and registry entries

Bad:

- a motion exists only inside one clip implementation
- an overlay is hidden inside a special-purpose composition
- the CLI reimplements render behavior for a single asset type
- the same visual rule is duplicated in multiple places

## Design Rules

1. Prefer shared utilities over special-case code.
2. Prefer explicit contracts over hidden behavior.
3. Prefer reusable presets over duplicate pipelines.
4. Prefer registries and builders over hardcoded branches.
5. Prefer adapters at the edge, not inside the core.
6. Prefer "one behavior, many consumers" over "one consumer, one behavior."

## Responsibility Boundaries

- `scene` defines what can exist.
- `runtime` evaluates and prepares what should happen.
- `render_graph` describes how the frame is executed.
- `backend` performs the concrete work.
- `CLI` chooses and wires capabilities.
- `tests` verify that shared behavior works across consumers.

The important part is that a utility should not care whether it is called by image, text, camera, or video, as long as the contract is valid.

## Practical Consequence

If a feature is useful in one place and mechanically reusable elsewhere, promote it to a shared utility.

If a feature only works in one place because it is too specialized, keep it local.

This is the balance:

- share when the behavior is genuinely common
- isolate when the behavior is genuinely specific

## Rule Of Thumb

If you can say:

> "this image could also use that camera motion"

or

> "this text card should get the same particles overlay"

then the code should probably expose a reusable contract instead of a one-off implementation.

That is the Chronon3d style:

> nothing is fixed to one owner if it can be safely shared by all.
