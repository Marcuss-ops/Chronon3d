# bgfx Premiere Camera

Imported prototype from the zip archive.

Stack: bgfx + GLFW + FFmpeg + ImGui + glm

Notes:
- The original sample targets Linux/X11 and is not wired into the current Filament build.
- It is kept here as a reference scaffold for a future camera/video pipeline.
- Shader compilation is still required before the sample can run.

Original build notes from the archive:

1. Install dependencies (Ubuntu):
   sudo apt install libglfw3-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libglm-dev

2. Build bgfx and imgui with vcpkg:
   vcpkg install bgfx glfw3 imgui[glfw-binding]

3. Compile shaders:
   shaderc -f shaders/vs_quad.sc -o shaders/vs.bin --type vertex --platform linux -p 120
   shaderc -f shaders/fs_quad.sc -o shaders/fs.bin --type fragment --platform linux -p 120

4. Build:
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
   make

5. Run:
   ./app /path/to/video.mp4
