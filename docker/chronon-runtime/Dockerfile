# syntax=docker/dockerfile:1.7
# Build from the Chronon3d repository root:
#   docker build -f docker/chronon-runtime/Dockerfile -t chronon3d-runtime:0.1.0-alpha.1 .

ARG VCPKG_REF=cb2981c4e03d421fa03b9bb5044cd1986180e7e4

FROM debian:bookworm-slim AS build-base

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

ARG VCPKG_REF

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build ccache git curl ca-certificates \
        pkg-config zip unzip tar python3-pip \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
        libvulkan-dev glslang-tools \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m pip install --break-system-packages --no-cache-dir 'cmake>=3.27' \
    && git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout --detach "$VCPKG_REF" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

FROM build-base AS deps

WORKDIR /src/chronon3d

# This stage depends only on the manifest, so normal source changes do not
# invalidate the dependency installation.
COPY vcpkg.json ./
COPY cmake/vcpkg-overlays/clipper2 ./cmake/vcpkg-overlays/clipper2

RUN --mount=type=cache,id=chronon-vcpkg-archives,target=/root/.cache/vcpkg/archives \
    --mount=type=cache,id=chronon-vcpkg-downloads,target=/opt/vcpkg/downloads \
    ( /opt/vcpkg/vcpkg install \
        --triplet x64-linux \
        --x-feature=vulkan \
        --overlay-ports=/src/chronon3d/cmake/vcpkg-overlays \
        --x-manifest-root=/src/chronon3d \
        --x-install-root=/opt/vcpkg_installed || { \
            echo 'vcpkg install failed; clipper2 build logs follow:' >&2; \
            cat /opt/vcpkg/buildtrees/clipper2/install-x64-linux-rel-out.log >&2 2>/dev/null || true; \
            cat /opt/vcpkg/buildtrees/clipper2/install-x64-linux-rel-err.log >&2 2>/dev/null || true; \
            exit 1; \
        } )

# ── map renderer (ChrononPlugin/plugins/maps) ──────────────────────────────────
# The native dynamic-map renderer (maps_dynamic_map_render) is the single map
# lane since the Python renderer's retirement (TICKET-MAPS-NATIVE-PORT):
# binary + boundary catalog install as one unit. ChrononPlugin and
# ChrononTemplate are cloned at pinned refs exactly like ChrononMotion3d
# below — advance the pins together with the plugin.
FROM deps AS maps

ARG CHRONONPLUGIN_REF=c000ee3b7cba8d8d35aa1faddd6c8ff32b067a3f
ARG CHRONONTEMPLATE_REF=7812872e7c9ade2ce0202025be3d09478f438324

# python3 is only needed at build time: the plugin's CMake registers the
# parity-gate tests behind a Python interpreter. None of it reaches the image.
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        libopencv-dev python3 \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/Marcuss-ops/ChrononPlugin.git /src/chrononplugin \
    && git -C /src/chrononplugin fetch --depth 1 origin "${CHRONONPLUGIN_REF}" \
    && git -C /src/chrononplugin checkout --detach FETCH_HEAD
RUN git clone https://github.com/Marcuss-ops/ChrononTemplate.git /src/chronontemplate \
    && git -C /src/chronontemplate fetch --depth 1 origin "${CHRONONTEMPLATE_REF}" \
    && git -C /src/chronontemplate checkout --detach FETCH_HEAD

# The plugin resolves Clipper2/nlohmann_json from the manifest tree above and
# prefers a system OpenCV (libopencv-dev), so no static bootstrap is needed.
RUN cmake -S /src/chrononplugin/plugins/maps -B /src/chrononplugin/build/docker -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/chrononmaps \
        -DCHRONON_MAPS_CATALOG_DIR=/src/chronontemplate/catalog \
        -DClipper2_DIR=/opt/vcpkg_installed/x64-linux/share/clipper2 \
        -Dnlohmann_json_DIR=/opt/vcpkg_installed/x64-linux/share/nlohmann_json \
    && cmake --build /src/chrononplugin/build/docker --parallel \
    && cmake --install /src/chrononplugin/build/docker

FROM deps AS builder

ARG CHRONONMOTION_REF=30f5750d00982b4c413706513667b8f92b483171

WORKDIR /src/chronon3d
COPY . .

RUN git clone https://github.com/Marcuss-ops/ChrononMotion3d.git /src/chrononmotion \
    && git -C /src/chrononmotion fetch --depth 1 origin "${CHRONONMOTION_REF}" \
    && git -C /src/chrononmotion checkout --detach FETCH_HEAD

RUN --mount=type=cache,id=chronon-ccache,target=/root/.cache/ccache \
    cmake -S . -B build/docker -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCHRONON3D_BUILD_CLI=ON \
        -DCHRONON3D_BUILD_C_API=ON \
        -DCHRONON3D_BUILD_TESTS=OFF \
        -DCHRONON3D_BUILD_CONTENT=OFF \
        -DCHRONON3D_BUILD_DIAGNOSTICS=OFF \
        -DCHRONON3D_ENABLE_TEXT=ON \
        -DCHRONON3D_ENABLE_VIDEO=ON \
        -DCHRONON3D_ENABLE_NATIVE_FFMPEG=ON \
        -DCHRONON3D_ENABLE_VULKAN=ON \
        -DCHRONON3D_USE_BLEND2D=ON \
        -DCHRONON3D_ENABLE_MOTION_ADAPTER=ON \
        -DCHRONONMOTION_SOURCE_DIR=/src/chrononmotion \
    && cmake --build build/docker --target chronon3d_cli chronon3d_c chronon3d_sdk_impl chronon3d_camera_plan_bake --parallel \
    && cmake --install build/docker --prefix /opt/chronon3d

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/chronon3d/lib

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        ca-certificates ffmpeg libstdc++6 libc-bin libvulkan1 \
        libopencv-core406 libopencv-imgproc406 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home --shell /usr/sbin/nologin chronon

COPY --from=builder /opt/chronon3d /opt/chronon3d
COPY --from=maps /opt/chrononmaps /opt/chrononmaps

# The native dynamic-map renderer and its boundary catalog (the single map
# lane). PipelineGen resolves the renderer through these two variables.
ENV VELOX_MAP_NATIVE_RENDERER_PATH=/opt/chrononmaps/bin/maps_dynamic_map_render
ENV CHRONON_MAPS_CATALOG_DIR=/opt/chrononmaps/share/chrononplugin/maps-catalog

RUN mkdir -p /work/assets /work/output \
    && chown -R chronon:chronon /work

USER chronon
WORKDIR /work
ENTRYPOINT ["/opt/chronon3d/bin/chronon3d_cli"]
