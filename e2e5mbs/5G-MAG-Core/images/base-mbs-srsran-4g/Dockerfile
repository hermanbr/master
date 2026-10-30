# syntax=docker/dockerfile:1

# global build-time arguments for FROM statements
# (https://docs.docker.com/engine/reference/builder/#understand-how-arg-and-from-interact)

ARG UBUNTU_VERSION="jammy"

FROM ubuntu:${UBUNTU_VERSION}

# build-time arguments
ARG SRSRAN_4G_BRANCH="5mbs"
ARG DEBIAN_FRONTEND="noninteractive"

# install dependencies
RUN apt-get update && apt-get install build-essential cmake libfftw3-dev libmbedtls-dev \
    libboost-program-options-dev libconfig++-dev libsctp-dev libzmq3-dev git libtool -y

# clone czmq
RUN git clone --depth 1 https://github.com/zeromq/czmq.git

# install czmq
RUN cd czmq && ./autogen.sh && ./configure && make && make install && ldconfig

# clone the specific srsRAN_4G version
RUN git clone --depth 1 --branch=${SRSRAN_4G_BRANCH} https://github.com/5G-MAG/srsRAN_4G

# install srsRAN_4G
RUN cd srsRAN_4G && mkdir build && cd build && cmake ../ -DENABLE_SRSEPC=OFF -DENABLE_SRSENB=OFF -DENABLE_GUI=OFF \
    && make -j `nproc`