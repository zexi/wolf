#!/bin/bash

TAG="ports.1"

docker build \
  -t "registry.cn-beijing.aliyuncs.com/zexi/wolf:$TAG" \
  --build-arg HTTPS_PROXY="http://192.168.167.128:7890" --build-arg HTTP_PROXY="http://192.168.167.128:7890" \
  -f ./docker/wolf.Dockerfile .
