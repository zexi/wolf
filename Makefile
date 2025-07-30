TAG := v0.0.3-https.1

build:
	TAG=$(TAG) bash -x ./docker-build.sh

build-start: build
	TAG=$(TAG) bash -x ./start-wolf-etc.sh

build-gstreamer:
	docker buildx build --platform linux/amd64 --push \
		-t registry.cn-beijing.aliyuncs.com/zexi/gstreamer:20250416.0 \
		-f ./docker/gstreamer.Dockerfile .
