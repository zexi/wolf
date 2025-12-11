#!/bin/bash

TAG="${TAG:-hook.1}"
IMG="registry.cn-beijing.aliyuncs.com/zexi/wolf:$TAG"
# BUILDER_IMAGE 通过环境变量从 Makefile 传入，如果没有设置则使用默认值
BUILDER_IMAGE="${BUILDER_IMAGE:-registry.cn-beijing.aliyuncs.com/zexi/wolf-builder:latest}"

#PROXY_SERVER=192.168.6.60
  #--build-arg HTTPS_PROXY="http://$PROXY_SERVER:7890" --build-arg HTTP_PROXY="http://$PROXY_SERVER:7890" --build-arg ALL_PROXY="socks5://$PROXY_SERVER:7890" \

docker buildx build --platform linux/amd64 --push \
  --build-arg BUILDER_IMAGE="$BUILDER_IMAGE" \
  -t "$IMG" \
  -f ./docker/wolf.Dockerfile .

#docker push $IMG
