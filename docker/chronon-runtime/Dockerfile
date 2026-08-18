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

RUN --mount=type=cache,id=chronon-vcpkg-archives,target=/root/.cache/vcpkg/archives \
    --mount=type=cache,id=chronon-vcpkg-downloads,target=/opt/vcpkg/downloads \
    /opt/vcpkg/vcpkg install \
        --triplet x64-linux \
        --x-manifest-root=/src/chronon3d \
        --x-install-root=/opt/vcpkg_installed

FROM deps AS builder

WORKDIR /src/chronon3d
COPY . .

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
    && cmake --build build/docker --target chronon3d_cli chronon3d_c chronon3d_sdk_impl --parallel \
    && cmake --install build/docker --prefix /opt/chronon3d

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/chronon3d/lib

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        ca-certificates ffmpeg libstdc++6 libc-bin libvulkan1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home --shell /usr/sbin/nologin chronon

COPY --from=builder /opt/chronon3d /opt/chronon3d
COPY VERSION /opt/chronon3d/VERSION

RUN mkdir -p /work/assets /work/output \
    && chown -R chronon:chronon /work

USER chronon
WORKDIR /work
ENTRYPOINT ["/opt/chronon3d/bin/chronon3d_cli"]
