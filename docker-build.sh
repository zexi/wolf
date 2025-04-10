#!/bin/bash

TAG="${TAG:-hook.1}"
IMG="registry.cn-beijing.aliyuncs.com/zexi/wolf:$TAG"

  # --build-arg HTTPS_PROXY="http://192.168.167.128:7890" --build-arg HTTP_PROXY="http://192.168.167.128:7890" \
  # --build-arg HTTPS_PROXY="http://192.168.6.60:7890" --build-arg HTTP_PROXY="http://192.168.6.60:7890" \
docker buildx build --platform linux/amd64 --push \
  -t "$IMG" \
  -f ./docker/wolf.Dockerfile .

#docker push $IMG
