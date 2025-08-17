FROM yg397/thrift-microservice-deps:xenial

# Install MongoDB C driver dependencies
RUN apt-get update && apt-get install -y \
    libmongoc-dev \
    libbson-dev \
    && rm -rf /var/lib/apt/lists/*

COPY ./ /media-microservices
RUN cd /media-microservices \
    && mkdir -p build \
    && cd build \
    && cmake .. \
    && make \
    && make install

WORKDIR /media-microservices
