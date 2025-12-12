#ARG BASE_IMAGE=ghcr.io/games-on-whales/gstreamer:1.26.2
ARG BASE_IMAGE=registry.cn-beijing.aliyuncs.com/zexi/gstreamer:1.26.7
# 使用预构建的 builder 基础镜像，包含 Rust 和所有构建依赖
# 默认使用远程镜像，如果本地有构建可以使用 wolf-builder:latest
ARG BUILDER_IMAGE=registry.cn-beijing.aliyuncs.com/zexi/wolf-builder:latest
########################################################
FROM $BUILDER_IMAGE AS wolf-builder

COPY . /wolf/
WORKDIR /wolf

ENV CCACHE_DIR=/cache/ccache
ENV CMAKE_BUILD_DIR=/cache/cmake-build
# 清理旧的 CMake 缓存（源代码路径已改变），但保留 _deps 目录中的预下载依赖
RUN --mount=type=cache,target=/cache/ccache \
    <<_BUILD
    #!/bin/bash
    set -e
    
    # 如果构建目录存在，清理 CMake 缓存文件，但保留 _deps 目录
    if [ -d "$CMAKE_BUILD_DIR" ]; then
        echo "Cleaning CMake cache but preserving _deps directory..."
        find "$CMAKE_BUILD_DIR" -mindepth 1 -maxdepth 1 ! -name "_deps" -exec rm -rf {} + || true
    fi
    
    # 运行 CMake 配置和构建
    cmake -B$CMAKE_BUILD_DIR \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_CXX_EXTENSIONS=OFF \
        -DCMAKE_CXX_FLAGS="-Wno-missing-template-arg-list-after-template-kw" \
        -DBUILD_SHARED_LIBS=OFF \
        -DBoost_USE_STATIC_LIBS=ON \
        -DBUILD_FAKE_UDEV_CLI=ON \
        -DBUILD_TESTING=OFF \
        -G Ninja
    
    ninja -j 10 -C $CMAKE_BUILD_DIR wolf
    ninja -j 10 -C $CMAKE_BUILD_DIR fake-udev
    
    # 复制构建产物
    cp $CMAKE_BUILD_DIR/src/moonlight-server/wolf /wolf/wolf
    cp $CMAKE_BUILD_DIR/src/fake-udev/fake-udev /wolf/fake-udev
_BUILD

########################################################
FROM $BASE_IMAGE AS runner
ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's/security.ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list
RUN sed -i 's/archive.ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources
# Wolf runtime dependencies
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3 \
    libicu76 \
    libevdev2 \
    libudev1 \
    libcurl4 \
    libdrm2 \
    libpci3 \
    libunwind8 \
    && rm -rf /var/lib/apt/lists/*

# gst-plugin-wayland runtime dependencies
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    libwayland-server0 libinput10 libxkbcommon0 libgbm1 \
    libglvnd0 libgl1 libglx0 libegl1 libgles2 xwayland hwdata \
    && rm -rf /var/lib/apt/lists/*

ENV GST_PLUGIN_PATH=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/
# Copying out our custom compositor from the build stage
COPY --from=wolf-builder /usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/* $GST_PLUGIN_PATH
COPY --from=wolf-builder /usr/local/lib/liblibgstwaylanddisplay* /usr/local/lib/

WORKDIR /wolf

ENV WOLF_CFG_FOLDER=/etc/wolf/cfg

COPY --from=wolf-builder /wolf/wolf /wolf/wolf
COPY --from=wolf-builder /wolf/fake-udev /wolf/fake-udev

ENV GST_GL_API=gles2 \
    GST_GL_PLATFORM=egl \
    GST_GL_WINDOW=surfaceless \
    WOLF_USE_ZERO_COPY=TRUE \
    WOLF_LOG_LEVEL=INFO \
    WOLF_CFG_FILE=$WOLF_CFG_FOLDER/config.toml \
    WOLF_PRIVATE_KEY_FILE=$WOLF_CFG_FOLDER/key.pem \
    WOLF_PRIVATE_CERT_FILE=$WOLF_CFG_FOLDER/cert.pem \
    WOLF_PULSE_IMAGE=ghcr.io/games-on-whales/pulseaudio:master \
    WOLF_RENDER_NODE=/dev/dri/renderD128 \
    WOLF_STOP_CONTAINER_ON_EXIT=TRUE \
    WOLF_DOCKER_SOCKET=/var/run/docker.sock \
    RUST_BACKTRACE=full \
    RUST_LOG=WARN \
    HOST_APPS_STATE_FOLDER=/etc/wolf \
    GST_DEBUG=2 \
    PUID=0 \
    PGID=0 \
    UNAME="root"

# Setting up XDG_RUNTIME_DIR this will automatically create a volume when starting the container
VOLUME /run/user/wolf/
ENV XDG_RUNTIME_DIR=/run/user/wolf

# HTTPS
EXPOSE 47984/tcp
# HTTP
EXPOSE 47989/tcp
# Control
EXPOSE 47999/udp
# RTSP
EXPOSE 48010/tcp
# Video
EXPOSE 48100/udp
# Audio
EXPOSE 48200/udp

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="Wolf: stream virtual desktops and games in Docker"

# See GOW/base-app
COPY --chmod=777 docker/startup.sh /opt/gow/startup-app.sh
ENTRYPOINT ["/entrypoint.sh"]
