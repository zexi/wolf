#!/bin/bash


  # -v /tmp/sockets/pulse-socket:/tmp/sockets/pulse-socket \
  # -v /tmp/sockets/wayland-2:/tmp/sockets/wayland-2 \

docker run --rm \
  --name lzx-steam \
  -v /etc/wolf/fake-udev:/usr/bin/fake-udev:ro \
  -v /etc/wolf/18046928878093460462/Steam:/home/retro \
  -v /tmp/sockets:/tmp/sockets \
  -v /var/lib/docker/volumes/nvidia-driver-vol/_data:/usr/nvidia \
  -v /etc/wolf/18046928878093460462/Steam/udev:/run/udev \
  --security-opt seccomp=unconfined --security-opt apparmor=unconfined \
  --ipc host \
  --ulimit nofile=10240:10240 \
  --device /dev/nvidia-uvm \
  --device /dev/nvidia-uvm-tools \
  --device /dev/dri/card0 \
  --device /dev/dri/renderD128 \
  --device /dev/nvidia-caps/nvidia-cap1 \
  --device /dev/nvidia-caps/nvidia-cap2 \
  --device /dev/nvidiactl \
  --device /dev/nvidia0 \
  --device /dev/nvidia-modeset \
  --device /dev/uinput \
  --device /dev/uhid \
  --cap-add SYS_ADMIN \
  --cap-add SYS_NICE \
  --cap-add SYS_PTRACE \
  --cap-add NET_RAW \
  --cap-add MKNOD \
  --cap-add NET_ADMIN \
  --device-cgroup-rule "c 13:* rmw" \
  --device-cgroup-rule "c 244:* rmw" \
  -e "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
  -e UNAME=retro \
  -e UMASK=000 \
  -e HOME=/home/retro \
  -e TZ==Europe/London \
  -e NEEDRESTART_SUSPEND=1 \
  -e "DEBIAN_FRONTEND=noninteractive" \
  -e "NEEDRESTART_SUSPEND=1" \
  -e "GAMESCOPE_VERSION=3.15.14" \
  -e "BUILD_ARCHITECTURE=amd64" \
  -e "DEB_BUILD_OPTIONS=noddeb" \
  --entrypoint /opt/bin/wolf-hook \
  zexi/steam:custom.0 \
  -addr 0.0.0.0

# docker run --rm -ti \
#   --name lzx-steam \
#   -v /etc/wolf/fake-udev:/usr/bin/fake-udev:ro \
#   -v /etc/wolf/12804594947086551671/Steam:/home/retro \
#   -v /tmp/sockets:/tmp/sockets \
#   -v /var/lib/docker/volumes/nvidia-driver-vol/_data:/usr/nvidia \
#   -v /etc/wolf-20001/12804594947086551671/Steam/udev:/run/udev \
#   --device /dev/nvidia-uvm \
#   --device /dev/nvidia-uvm-tools \
#   --device /dev/dri/ \
#   --device /dev/nvidia-caps/nvidia-cap1 \
#   --device /dev/nvidia-caps/nvidia-cap2 \
#   --device /dev/nvidiactl \
#   --device /dev/nvidia0 \
#   --device /dev/nvidia-modeset \
#   --device /dev/uinput \
#   --device /dev/uhid \
#   --entrypoint bash \
#   zexi/steam:custom.0 \
