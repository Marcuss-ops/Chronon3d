# Motion-Bear reference split: ten numbered studies

This suite treats the 28.567-second, 960×540, 30 fps preview as ten numbered animation groups and renders them at 1920×1080, 30 fps. Phrases that change inside a numbered group stay in that clip: #01 Motion Bear/Presents, #04 Save time/boost your/workflow, #08 Easily/adjust duration, #09 Video/Tutorial/Included.

Generate the ten plans and stage image with the C++ authoring program, then render through the Chronon C++ CLI:

```bash
bash Chronon3d/tests/fixtures/text_motion_reference_v2/run.sh
```

The runner validates and exports ten isolated MP4s at 1920×1080, 30 fps to `ChrononTemplate/out/text_motion_reference_v3_1080p/`, using the pipe encoder uniformly. The native muxer lost frames from the 137-frame #10 export while returning success; that drain (`EAGAIN` flush) defect was fixed on 2026-09-23 and the owning ticket is now purged (git history is the archive). `manifest.json` records the source-frame boundaries. Apple restricts SF Pro to mockups for Apple-platform software, so these general motion-graphics renders use the bundled DM Sans Regular as a close, redistributable substitute. The current render-plan exposes a single Gaussian glow; the clips use that supported contract and do not claim a multi-scale bloom implementation.
