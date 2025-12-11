#ARG BASE_IMAGE=ghcr.io/games-on-whales/gstreamer:1.26.2
ARG BASE_IMAGE=registry.cn-beijing.aliyuncs.com/zexi/gstreamer:1.26.7
########################################################
FROM $BASE_IMAGE AS wolf-builder-base

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's/archive.ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    curl \
    ca-certificates \
    ninja-build \
    cmake \
    pkg-config \
    ccache \
    git \
    clang \
    build-essential \
    libboost-thread-dev libboost-locale-dev libboost-filesystem-dev libboost-log-dev libboost-stacktrace-dev libboost-container-dev \
    libwayland-dev libwayland-server0 libinput-dev libxkbcommon-dev libgbm-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libevdev-dev \
    libpulse-dev \
    libunwind-dev \
    libudev-dev \
    libdrm-dev \
    libpci-dev \
    libglib2.0-dev libegl-dev libgles-dev libopengl-dev \
    && rm -rf /var/lib/apt/lists/*

## Install Rust in order to build our custom compositor
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="$HOME/.cargo/bin:${PATH}"

ARG RUST_VERSION=1.91.1
ENV RUST_VERSION=$RUST_VERSION
RUN rustup install $RUST_VERSION && rustup default $RUST_VERSION

WORKDIR /tmp/
RUN <<_GST_WAYLAND_DISPLAY
    #!/bin/bash
    set -e

    git clone https://github.com/games-on-whales/gst-wayland-display
    cd gst-wayland-display
    git checkout 67b1183
    cargo install cargo-c
    cargo cinstall --features="cuda" --prefix=/usr/local/lib/x86_64-linux-gnu/ --libdir=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0
_GST_WAYLAND_DISPLAY

# 预下载所有依赖以加速后续构建
# 使用 CMake 的 FetchContent 机制自动下载所有依赖到镜像内
WORKDIR /tmp/wolf-src

# 复制项目文件
COPY . ./

# 运行 cmake configure 来触发 FetchContent 下载所有依赖
# 依赖会自动下载到构建目录下的 _deps 子目录，保存在镜像中
RUN mkdir -p /cache/cmake-build && \
    cmake -B /cache/cmake-build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_EXTENSIONS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DBoost_USE_STATIC_LIBS=ON \
    -DBUILD_FAKE_UDEV_CLI=ON \
    -DBUILD_TESTING=OFF \
    -DBUILD_MOONLIGHT=ON \
    -DLINK_RUST_WAYLAND=OFF \
    -G Ninja \
    -S . || true

# 清理临时文件，但保留下载的依赖
WORKDIR /
RUN rm -rf /tmp/wolf-src
