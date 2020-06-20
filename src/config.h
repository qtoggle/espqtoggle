
/*
 * Copyright 2019 The qToggle Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H


#include <c_types.h>


#define CONFIG_OFFS_DEVICE_NAME         0x0000 /*   32 bytes */
#define CONFIG_OFFS_DEVICE_DISP_NAME    0x0020 /*   64 bytes */

#define CONFIG_OFFS_ADMIN_PASSWORD      0x0060 /*   32 bytes */
#define CONFIG_OFFS_NORMAL_PASSWORD     0x0080 /*   32 bytes */
#define CONFIG_OFFS_VIEWONLY_PASSWORD   0x00A0 /*   32 bytes */
#define CONFIG_OFFS_TCP_PORT            0x0140 /*    2 bytes */
                                               /* 0x0142 - 0x0143: reserved */
#define CONFIG_OFFS_DEVICE_FLAGS        0x0144 /*    4 bytes */
                                               /* 0x0148 - 0x014B: reserved */
#define CONFIG_OFFS_IP_ADDRESS          0x014C /*    4 bytes */
#define CONFIG_OFFS_GATEWAY             0x0150 /*    4 bytes */
#define CONFIG_OFFS_DNS                 0x0154 /*    4 bytes */
#define CONFIG_OFFS_NETMASK             0x0158 /*    1 bytes */
                                               /* 0x0159 - 0x015B: reserved */

#define CONFIG_OFFS_MODEL               0x015C /*    4 bytes - strings pool pointer */
                                               /* 0x0160 - 0x016F: reserved */

#define CONFIG_OFFS_WEBHOOKS_HOST       0x0170 /*    4 bytes - strings pool pointer */
#define CONFIG_OFFS_WEBHOOKS_PORT       0x0174 /*    2 bytes */
#define CONFIG_OFFS_WEBHOOKS_PATH       0x0176 /*    4 bytes - strings pool pointer */
#define CONFIG_OFFS_WEBHOOKS_PASSWORD   0x017A /*    4 bytes - strings pool pointer */
#define CONFIG_OFFS_WEBHOOKS_EVENTS     0x017E /*    2 bytes */
#define CONFIG_OFFS_WEBHOOKS_TIMEOUT    0x0180 /*    2 bytes */
#define CONFIG_OFFS_WEBHOOKS_RETRIES    0x0182 /*    1 bytes */
                                               /* 0x0183 - 0x018F: reserved */

#define CONFIG_OFFS_WAKE_INTERVAL       0x0190 /*    2 bytes */
#define CONFIG_OFFS_WAKE_DURATION       0x0192 /*    2 bytes */
                                               /* 0x0194 - 0x019F: reserved */

#define CONFIG_OFFS_PORT_BASE           0x0200 /*   96 bytes for each of the maximum 32 supported ports */
#define CONFIG_OFFS_PERIPHERALS_BASE    0x0E00 /*   64 bytes for each of the maximum 16 supported peripherals */
#define CONFIG_OFFS_STR_BASE            0x1200 /* 3584 bytes for strings pool */

#define CONFIG_STR_SIZE                 0x0E00 /* 3584 bytes */
#define CONFIG_PORT_SIZE                0x0060 /*   96 bytes for each port */
#define CONFIG_PERIPHERAL_SIZE          0x0040 /*   64 bytes for each peripheral */


/* Following items go into system flash configuration */

#define CONFIG_OFFS_SETUP_BUTTON_PIN    0x0000 /*    1 byte */
#define CONFIG_OFFS_SETUP_BUTTON_LEVEL  0x0001 /*    1 byte */
#define CONFIG_OFFS_SETUP_BUTTON_HOLD   0x0002 /*    1 byte */
#define CONFIG_OFFS_SETUP_BUTTON_HOLDR  0x0003 /*    1 byte */

#define CONFIG_OFFS_STATUS_LED_PIN      0x0004 /*    1 byte */
#define CONFIG_OFFS_STATUS_LED_LEVEL    0x0005 /*    1 byte */


ICACHE_FLASH_ATTR void                  config_init(void);
ICACHE_FLASH_ATTR void                  config_save(void);
ICACHE_FLASH_ATTR void                  config_start_provisioning(void);
ICACHE_FLASH_ATTR bool                  config_is_provisioning(void);


#endif /* _CONFIG_H */
