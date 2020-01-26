# Build with:
#     docker build -t qtoggle/espqtoggle-builder .

FROM ubuntu:19.04

MAINTAINER Calin Crisan ccrisan@gmail.com

ENV PATH="/opt/xtensa-lx106-elf/bin:${PATH}"
ENV SDK_BASE="/opt/esp-nonos-sdk"

# apt stuff
RUN DEBIAN_FRONTEND=noninteractive apt-get update -qq
RUN DEBIAN_FRONTEND=noninteractive apt-get install -yq \
    make \
    gcc \
    git \
    xz-utils \
    python \
    python-serial \
    s3cmd \
    wget \
    jq \
    gettext-base \
    curl \
    bsdmainutils

RUN apt-get clean

# xtensa-lx106 toolchain 
ADD https://qtoggle.s3.amazonaws.com/various/xtensa-lx106-elf.tar.xz /opt
RUN cd /opt && tar xvf xtensa-lx106-elf.tar.xz && rm xtensa-lx106-elf.tar.xz

# esp8266 sdk
ADD fix-sdk.sh /opt
RUN git clone https://github.com/espressif/ESP8266_NONOS_SDK.git /opt/esp-nonos-sdk && \
    cd /opt/esp-nonos-sdk && \
    git checkout a0b1311 && \
    /opt/fix-sdk.sh . && \
    rm -rf .git

ADD ./builder.sh /opt

CMD /opt/builder.sh
