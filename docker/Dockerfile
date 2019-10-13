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
    git checkout 88b1082 && \
    ln -s /opt/esp-nonos-sdk/bin/boot_v1.7.bin /opt/esp-nonos-sdk/bin/boot.bin && \
    ln -s /opt/esp-nonos-sdk/bin/esp_init_data_default_v08.bin /opt/esp-nonos-sdk/bin/esp_init_data_default.bin && \
    sed -ri 's/ESP_SDK_VERSION_NUMBER\s+0x020200/ESP_SDK_VERSION_NUMBER 0x020202/' /opt/esp-nonos-sdk/include/version.h && \
    sed -ri 's/ESP_SDK_VERSION_PATCH\s+0/ESP_SDK_VERSION_PATCH 2/' /opt/esp-nonos-sdk/include/version.h && \
    sed -ri 's/ESP_SDK_VERSION_STRING\s+"2.2.0"/ESP_SDK_VERSION_STRING "2.2.x-git88b1082"/' /opt/esp-nonos-sdk/include/version.h && \
    rm -rf .git

ENV PATH="/opt/xtensa-lx106-elf/bin:${PATH}"
ENV SDK_BASE="/opt/esp-nonos-sdk"

ADD ./builder.sh /opt

CMD /opt/builder.sh
