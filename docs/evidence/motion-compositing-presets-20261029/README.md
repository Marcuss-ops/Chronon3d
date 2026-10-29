# Motion compositing presets verification — 2026-10-29

## Checkout and host

- Chronon3D `HEAD`: `014eeb4dfd1703820d49b6382d127ab253bf1449` (dirty; 65 paths
  reported modified before this evidence update).
- Host: Linux `YOutube`, x86_64, kernel `5.15.0-198-generic`.
- GPU visible: NVIDIA RTX A4000.
- Build preset used for tests: `build/chronon/linux-video-test`, Debug,
  `BUILD_TESTING=ON`, `CHRONON3D_ENABLE_SVG=OFF`.
- CLI used for frame gates: `build/chronon/linux-video-release`, Release,
  `CHRONON3D_ENABLE_SVG=OFF`.

## Executed checks

1. Rebuilt the two focused test targets:

   ```sh
   cmake --build Chronon3d/build/chronon/linux-video-test \
     --target chronon3d_c_abi_tests chronon3d_renderer_core_tests -j4
   ```

   Exit 0.

2. C ABI regression suite:

   ```sh
   ctest --test-dir Chronon3d/build/chronon/linux-video-test \
     --output-on-failure -R '^chronon3d_c_abi_tests$'
   ```

   Exit 0 after rebuilding the target; 1/1 CTest passed (264.71 s).

3. Renderer core regression suite:

   ```sh
   ctest --test-dir Chronon3d/build/chronon/linux-video-test \
     --output-on-failure -R '^chronon3d_renderer_core_tests$'
   ```

   Relinked `chronon3d_renderer_core_tests` serially after the settings-type
   fix; exit 0; 1/1 CTest passed (13.57 s).

4. SVG CLI frame gate:

   ```sh
   CHRONON_CLI=/home/pierone/src/go-master/projects/Pyt/VeloxEditing/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
     python3 Chronon3d/scripts/ci/check_svg_cli_frame_gate.py
   ```

   Exit 1. The plan validator rejects the fixture with `Chronon was built
   without SVG import support`; the chosen preset has `CHRONON3D_ENABLE_SVG=OFF`.
   No rendered golden was produced.

5. Lottie CLI frame gate:

   ```sh
   CHRONON_CLI=/home/pierone/src/go-master/projects/Pyt/VeloxEditing/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
     python3 Chronon3d/scripts/ci/check_lottie_cli_frame_gate.py
   ```

   Exit 1 at `L1 render`. Reproduction of the first fixture shows the CLI
   validator accepts the plan, but export fails while opening `libx264`:
   `avcodec_find_encoder_by_name('libx264') failed`. Although system FFmpeg
   lists that encoder, the CLI's linked native AV runtime does not expose it.
   No Lottie golden was produced.

6. Full build attempt and resource policy:

   ```sh
   cmake --build Chronon3d/build/chronon/linux-video-test -j4
   ```

   The first attempt exposed an invalid/incomplete `RenderSettings` type in the
   pre-existing typed-render settings override. The override was corrected to
   use `::chronon3d::RenderSettings` with a global forward declaration. A
   second whole-tree build attempt was stopped after observing active
   unrelated model/daemon/compiler processes and swap at 3.5/4 GiB. It was not
   a build failure after the type correction. The focused C ABI target was
   rebuilt serially (`cmake --build ... --target chronon3d_c_abi_tests -j1`,
   exit 0) and its suite rerun above. The remaining full build and 104 other
   CTest entries were deliberately not started to avoid exhausting host memory.

## State

The two focused CTest targets pass, but this is not the complete CTest suite.
The SVG and Lottie frame gates did not reach pixel assertions, so they are
failures due to the selected local build/runtime configuration, not evidence of
render parity. Nested Bodymovin groups/transforms remain unimplemented per the
ticket's documented contract. Full-suite, SVG-enabled CLI, working CLI encoder,
rendered goldens, and same-SHA CI remain open.
