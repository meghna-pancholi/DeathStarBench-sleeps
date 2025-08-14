#!/bin/bash

docker build -t mm-loadgenerator -f Dockerfile .
LOADGENERATOR_IMAGE_ID=$(docker images --format="{{.Repository}} {{.ID}}" | grep "^mm-loadgenerator " | cut -d' ' -f2)
docker tag $LOADGENERATOR_IMAGE_ID meghnapancholi/mm-loadgenerator:latest-warmup
docker push meghnapancholi/mm-loadgenerator:latest-warmup