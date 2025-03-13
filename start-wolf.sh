#!/bin/bash
source /home/lzx/.wolf-dev-rc

WOLF_CFG_FOLDER=/etc/wolf/cfg
mkdir -p $WOLF_CFG_FOLDER
export XDG_RUNTIME_DIR=/tmp/sockets
export WOLF_LOG_LEVEL=INFO
export WOLF_CFG_FILE=$WOLF_CFG_FOLDER/config.toml
export WOLF_PRIVATE_KEY_FILE=$WOLF_CFG_FOLDER/key.pem
export WOLF_PRIVATE_CERT_FILE=$WOLF_CFG_FOLDER/cert.pem
export WOLF_PULSE_IMAGE=ghcr.io/games-on-whales/pulseaudio:master
export WOLF_RENDER_NODE=/dev/dri/renderD128
export WOLF_STOP_CONTAINER_ON_EXIT=TRUE
export WOLF_DOCKER_SOCKET=/var/run/docker.sock
export RUST_BACKTRACE=full
export RUST_LOG=WARN
export HOST_APPS_STATE_FOLDER=/etc/wolf
export GST_DEBUG=2
export PUID=0
export PGID=0
export UNAME="root"

/home/lzx/code/wolf/build/src/moonlight-server/wolf
