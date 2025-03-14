#!/bin/bash

sudo nvidia-container-cli --load-kmods info

docker rm wolf

docker run \
    --name wolf \
    --network=host \
    -e XDG_RUNTIME_DIR=/tmp/sockets \
    -v /tmp/sockets:/tmp/sockets:rw \
    -e WOLF_BASE_PORT=$1 \
    -e NVIDIA_DRIVER_VOLUME_NAME=nvidia-driver-vol \
    -v nvidia-driver-vol:/usr/nvidia:rw \
    -e HOST_APPS_STATE_FOLDER=/etc/wolf \
    -v /etc/wolf:/etc/wolf:rw \
    -v /var/run/docker.sock:/var/run/docker.sock:rw \
    --device /dev/nvidia-uvm \
    --device /dev/nvidia-uvm-tools \
    --device /dev/dri/ \
    --device /dev/nvidia-caps/nvidia-cap1 \
    --device /dev/nvidia-caps/nvidia-cap2 \
    --device /dev/nvidiactl \
    --device /dev/nvidia0 \
    --device /dev/nvidia-modeset \
    --device /dev/uinput \
    --device /dev/uhid \
    -v /dev/:/dev/:rw \
    -v /run/udev:/run/udev:rw \
    --device-cgroup-rule "c 13:* rmw" \
    registry.cn-beijing.aliyuncs.com/zexi/wolf:ports.0
