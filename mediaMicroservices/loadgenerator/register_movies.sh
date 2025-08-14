#!/usr/bin/env bash

# get IP_ADDRESS from first argument
IP_ADDRESS=$1

for i in {1..1000}; do
  curl -d "title=title_"$i"&movie_id=movie_id_"$i \
      http://${IP_ADDRESS}:8080/wrk2-api/movie/register

done
