#!/bin/bash

#docker build -t thrift-microservice-deps -f docker/thrift-microservice-deps/cpp/Dockerfile . 
docker build -t media -f Dockerfile .
SOCIAL_IMAGE_ID=$(docker images --format="{{.Repository}} {{.ID}}" | grep "^media " | cut -d' ' -f2)
docker tag $SOCIAL_IMAGE_ID meghnapancholi/dsb-media:sleeps 
docker push meghnapancholi/dsb-media:sleeps 

#docker build -t thrift-microservice-deps-asan -f docker/thrift-microservice-deps/cpp/Dockerfile-asan . 
#docker build -t media-asan -f Dockerfile-asan . 
#MEDIA_ASAN_IMAGE_ID=$(docker images --format="{{.Repository}} {{.ID}}" | grep "^media-asan " | cut -d' ' -f2)
#docker tag $MEDIA_ASAN_IMAGE_ID 520842413394.dkr.ecr.us-east-1.amazonaws.com/media-asan:latest
#docker push 520842413394.dkr.ecr.us-east-1.amazonaws.com/media-asan:latest


# docker build -t openresty-thrift -f docker/openresty-thrift/xenial/Dockerfile docker/openresty-thrift/.
# NGINX_IMAGE_ID=$(docker images --format="{{.Repository}} {{.ID}}" | grep "^openresty-thrift " | cut -d' ' -f2)
# docker tag $NGINX_IMAGE_ID 520842413394.dkr.ecr.us-east-1.amazonaws.com/openresty-thrift
# docker push 520842413394.dkr.ecr.us-east-1.amazonaws.com/openresty-thrift
