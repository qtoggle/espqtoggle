#!/bin/bash

function show_usage() {
    echo "Usage: $0 <SDK_BASE>"
    echo "Applies various fixes to an existing ESP8266 non-OS SDK."
}

SDK_BASE="$1"

if [[ -z "${SDK_BASE}" ]]; then
    show_usage
    exit 1
fi

if ! grep -q ' int16;' ${SDK_BASE}/include/c_types.h; then
    echo "Adding int16 to include/c_types.h"
    sed -ri 's/(typedef signed short\s+sint16;)/\1\ntypedef signed short        int16;/' \
        ${SDK_BASE}/include/c_types.h
fi

if ! grep -q ' int64;' ${SDK_BASE}/include/c_types.h; then
    echo "Adding int64 to include/c_types.h"
    sed -ri 's/(typedef signed long long\s+sint64;)/\1\ntypedef signed long long    int64;/' \
        ${SDK_BASE}/include/c_types.h
fi

if ! grep -q 'ets_vsnprintf' ${SDK_BASE}/include/osapi.h; then
    echo "Adding ets_vsnprintf to include/osapi.h"
    sed -ri 's/(#include <string.h>)/\1\n#include <stdarg.h>/' ${SDK_BASE}/include/osapi.h
    sed -ri 's/(int ets_snprintf.*)/\1\nint ets_vsnprintf(char *str, size_t size, const char *format, va_list args) __attribute__ ((format (printf, 3, 0)));/' \
        ${SDK_BASE}/include/osapi.h
    sed -ri 's/(#define os_snprintf\(.*)/\1\n\n#define os_vsnprintf  ets_vsnprintf/' ${SDK_BASE}/include/osapi.h
fi

if grep -q 'ets_intr_lock' ${SDK_BASE}/ld/eagle.rom.addr.v6.ld; then
    echo "Removing ets_intr_lock from ld/eagle.rom.addr.v6.ld"
    grep -v 'ets_intr_lock' ${SDK_BASE}/ld/eagle.rom.addr.v6.ld > ${SDK_BASE}/ld/eagle.rom.addr.v6.ld.bak
    mv ${SDK_BASE}/ld/eagle.rom.addr.v6.ld.bak ${SDK_BASE}/ld/eagle.rom.addr.v6.ld
fi

if grep -q 'ets_intr_unlock' ${SDK_BASE}/ld/eagle.rom.addr.v6.ld; then
    echo "Removing ets_intr_unlock from ld/eagle.rom.addr.v6.ld"
    grep -v 'ets_intr_unlock' ${SDK_BASE}/ld/eagle.rom.addr.v6.ld > ${SDK_BASE}/ld/eagle.rom.addr.v6.ld.bak
    mv ${SDK_BASE}/ld/eagle.rom.addr.v6.ld.bak ${SDK_BASE}/ld/eagle.rom.addr.v6.ld
fi

if ! grep -q 'ets_post_rom' ${SDK_BASE}/ld/eagle.rom.addr.v6.ld; then
    echo "Renaming ets_post to ets_post_rom in ld/eagle.rom.addr.v6.ld"
    sed -ri 's/ets_post/ets_post_rom/' ${SDK_BASE}/ld/eagle.rom.addr.v6.ld
fi

if ! [[ -f ${SDK_BASE}/bin/boot.bin ]]; then
    echo "Symlinking bin/boot.bin to bin/boot_v1.7.bin"
    ln -s boot_v1.7.bin ${SDK_BASE}/bin/boot.bin
fi

if ! [[ -f ${SDK_BASE}/bin/esp_init_data_default.bin ]]; then
    echo "Symlinking bin/esp_init_data_default.bin to bin/esp_init_data_default_v08.bin"
    ln -s esp_init_data_default_v08.bin ${SDK_BASE}/bin/esp_init_data_default.bin
fi

if grep -q '0x020200' ${SDK_BASE}/include/version.h; then
    echo "Patching version to 2.2.2 in include/version.h"
    sed -ri 's/ESP_SDK_VERSION_NUMBER\s+0x020200/ESP_SDK_VERSION_NUMBER 0x020202/' ${SDK_BASE}/include/version.h
    sed -ri 's/ESP_SDK_VERSION_PATCH\s+0/ESP_SDK_VERSION_PATCH 2/' ${SDK_BASE}/include/version.h
    version=$(cd ${SDK_BASE} && git describe --tags 2>/dev/null)
    if [[ -n "${version}" ]]; then
        echo "Setting version string to $version in include/version.h"
        sed -ri 's/ESP_SDK_VERSION_STRING\s+"2.2.0"/ESP_SDK_VERSION_STRING "'${version}'"/' ${SDK_BASE}/include/version.h
    fi
fi

if ! grep -qE "DHCP_DOES_ARP_CHECK\s+0" ${SDK_BASE}/third_party/include/lwipopts.h; then
    echo "Disabling DHCP ARP check in third_party/include/lwipopts.h"
    sed -ri 's/define DHCP_DOES_ARP_CHECK.*/define DHCP_DOES_ARP_CHECK 0/' ${SDK_BASE}/third_party/include/lwipopts.h
    make -C ${SDK_BASE}/third_party/lwip COMPILE=gcc >/dev/null
    cp ${SDK_BASE}/third_party/lwip/.output/eagle/debug/lib/liblwip.a lib
fi

if ! grep -qE "FUNC_U0RXD" ${SDK_BASE}/include/eagle_soc.h; then
    echo "Adding FUNC_U0RXD to include/eagle_soc.h"
    sed -ri 's/(#define FUNC_U0TXD\s+0)/\1\n#define FUNC_U0RXD 0/' ${SDK_BASE}/include/eagle_soc.h
fi

if ! [[ -f ${SDK_BASE}/.fixed ]]; then
    echo "Marking SDK as fixed"
    touch ${SDK_BASE}/.fixed
fi

echo "Done"
