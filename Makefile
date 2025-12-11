TAG := static-20251212.1
BUILDER_TAG := 20251212.2

.PHONY: build

build:
	TAG=$(TAG) BUILDER_IMAGE=registry.cn-beijing.aliyuncs.com/zexi/wolf-builder:$(BUILDER_TAG) bash -x ./docker-build.sh

build-start: build
	TAG=$(TAG) bash -x ./start-wolf-etc.sh

build-gstreamer:
	docker buildx build --platform linux/amd64 --push \
		-t registry.cn-beijing.aliyuncs.com/zexi/gstreamer:20250416.0 \
		-f ./docker/gstreamer.Dockerfile .

build-builder:
	docker buildx build --platform linux/amd64 --push \
		-t registry.cn-beijing.aliyuncs.com/zexi/wolf-builder:$(BUILDER_TAG) \
		-f ./docker/wolf-builder.Dockerfile .
