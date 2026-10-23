# Documentary Snapshot — Snap-down connected visual review

Rendered with the rebuilt `chronon3d_cli` software backend at 1920×1080 from
`documentary_snapshot_adapter_canary_v1.plan.json`. The five decoded frames
are 0, 45, 53, 80, and 120. The contact sheet and individual PNGs are the
visual evidence; raw RGBA files were removed after decoding.

Review: the title is readable during the title hold, the photo handoff is
complete by frame 53, and the final card is centered with 7 px margins. The
decoded photo/frame aspect is 1.3036:1 versus 1.3043:1 for the source.

The RenderPlan compiler's forced negative X scale for 3D text caused the
mirrored title seen in the first diagnostic. Removing that reflection fixes
the title in the rebuilt CLI. The other eleven recipe transitions, the full
temporal-shutter Vulkan path, and the 601-frame torture render remain open.

Files:

- `contact-sheet.webp`
- `documentary_snapshot_adapter_canary_v1.plan.json`
- `documentary_snapshot_adapter_frame_000.png`
- `documentary_snapshot_adapter_frame_045.png`
- `documentary_snapshot_adapter_frame_053.webp`
- `documentary_snapshot_adapter_frame_080.webp`
- `documentary_snapshot_adapter_frame_120.webp`
