# ${PROJECT_NAME}

Minimal standalone Chronon3D project. `chronon.json` identifies the executable,
and each composition is a canonical RenderPlan under `compositions/`.

## Build and render

Install the Chronon3D SDK, then run:

```bash
cmake -B build
cmake --build build
chronon render HelloWorld -o hello.mp4
```

The final command must run from this project directory. It dispatches to
`build/${PROJECT_NAME}`. The standalone executable uses the software renderer
unless the plan requests a strict backend.

## Add a composition

Add `compositions/<Name>.json` using the RenderPlan schema and invoke:

```bash
chronon render <Name> -o <output.mp4>
```

Names may contain letters, digits, hyphens, and underscores. Project assets
belong in `assets/`; paths in plans resolve from that directory.
