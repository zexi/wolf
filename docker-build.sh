#!/bin/bash

TAG="ports.8-pulse.1"
IMG="registry.cn-beijing.aliyuncs.com/zexi/wolf:$TAG"

  # --build-arg HTTPS_PROXY="http://192.168.167.128:7890" --build-arg HTTP_PROXY="http://192.168.167.128:7890" \
docker build \
  --build-arg HTTPS_PROXY="http://192.168.6.60:7890" --build-arg HTTP_PROXY="http://192.168.6.60:7890" \
  -t "$IMG" \
  -f ./docker/wolf.Dockerfile .

docker push $IMG
