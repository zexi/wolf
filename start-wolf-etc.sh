#!/bin/bash

TAG="${TAG:-hook.1}"
IMG="registry.cn-beijing.aliyuncs.com/zexi/wolf:$TAG"

sudo nvidia-container-cli --load-kmods info

docker rm wolf

    #-e WOLF_EXTERNAL_IP=192.168.6.254 \
    # -e WOLF_EXTERNAL_IP=111.202.78.70 \
docker run \
    --name wolf \
    --network=host \
    -e WOLF_LOG_LEVEL=debug \
    -e WOLF_BASE_PORT=20105 \
    -e WOLF_EXTERNAL_IP=192.168.6.60 \
    -e XDG_RUNTIME_DIR=/tmp/sockets \
    -v /tmp/sockets:/tmp/sockets:rw \
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
    $IMG
