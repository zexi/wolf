#!/bin/bash

TAG="${TAG:-hook.1}"
IMG="registry.cn-beijing.aliyuncs.com/zexi/wolf:$TAG"

#PROXY_SERVER=192.168.6.60
  #--build-arg HTTPS_PROXY="http://$PROXY_SERVER:7890" --build-arg HTTP_PROXY="http://$PROXY_SERVER:7890" --build-arg ALL_PROXY="socks5://$PROXY_SERVER:7890" \

docker buildx build --platform linux/amd64 --push \
  -t "$IMG" \
  -f ./docker/wolf.Dockerfile .

#docker push $IMG
