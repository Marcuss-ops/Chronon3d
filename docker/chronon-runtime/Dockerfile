# Build from the Chronon3d repository root:
#   docker build -f docker/chronon-runtime/Dockerfile -t chronon3d-runtime:0.1.0-alpha.1 .

FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        pkg-config zip unzip tar python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m pip install --break-system-packages --no-cache-dir 'cmake>=3.27' \
    && git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src/chronon3d
COPY . .

RUN cmake -S . -B build/docker -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DCHRONON3D_BUILD_CLI=ON \
        -DCHRONON3D_BUILD_C_API=ON \
        -DCHRONON3D_BUILD_TESTS=OFF \
        -DCHRONON3D_BUILD_CONTENT=OFF \
        -DCHRONON3D_BUILD_DIAGNOSTICS=OFF \
        -DCHRONON3D_ENABLE_TEXT=ON \
        -DCHRONON3D_ENABLE_VIDEO=ON \
        -DCHRONON3D_USE_BLEND2D=ON \
    && cmake --build build/docker --target chronon3d_cli chronon3d_c --parallel \
    && cmake --install build/docker --prefix /opt/chronon3d

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/chronon3d/lib

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        ca-certificates ffmpeg libstdc++6 libc-bin \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home --shell /usr/sbin/nologin chronon

COPY --from=builder /opt/chronon3d /opt/chronon3d

RUN mkdir -p /work/assets /work/output \
    && chown -R chronon:chronon /work

USER chronon
WORKDIR /work
ENTRYPOINT ["/opt/chronon3d/bin/chronon3d_cli"]
