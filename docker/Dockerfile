# Build with:
#     docker build -t qtoggle/espqtoggle-builder .

FROM ubuntu:19.04

MAINTAINER Calin Crisan ccrisan@gmail.com

# apt stuff
RUN DEBIAN_FRONTEND=noninteractive apt-get update -qq
RUN DEBIAN_FRONTEND=noninteractive apt-get install -yq \
    make \
    git \
    xz-utils \
    python \
    python-serial \
    s3cmd \
    wget \
    jq \
    gettext-base \
    curl

RUN apt-get clean

# xtensa-lx106 toolchain 
ADD https://gitlab.com/qtoggle/espqtoggle/uploads/8f1b68d934f365a9563e36aa9096b006/xtensa-lx106-elf.tar.xz /opt
RUN cd /opt && tar xvf xtensa-lx106-elf.tar.xz && rm xtensa-lx106-elf.tar.xz

# esp8266 sdk
RUN git clone https://github.com/espressif/ESP8266_NONOS_SDK.git /opt/esp-nonos-sdk && \
    cd /opt/esp-nonos-sdk && \
    git checkout v2.2.1 && \
    rm -rf .git

RUN wget https://raw.githubusercontent.com/espressif/esptool/v2.6/esptool.py -O /usr/bin/esptool && chmod +x /usr/bin/esptool

ENV PATH="/opt/xtensa-lx106-elf/bin:${PATH}"
ENV SDK_BASE="/opt/esp-nonos-sdk"

ADD ./builder.sh /opt

CMD /opt/builder.sh
