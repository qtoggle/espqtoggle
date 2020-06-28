
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

#ifndef _DEVICE_H
#define _DEVICE_H


#ifdef _DEBUG_DEVICE
#define DEBUG_DEVICE(fmt, ...)          DEBUG("[device        ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_DEVICE(...)               {}
#endif

#define DEFAULT_TCP_PORT                80

#define DEVICE_FLAG_WEBHOOKS_ENABLED    0x00000001
#define DEVICE_FLAG_WEBHOOKS_HTTPS      0x00000002
#define DEVICE_FLAG_OTA_AUTO_UPDATE     0x00000004
#define DEVICE_FLAG_OTA_BETA_ENABLED    0x00000008


extern char                           * device_name;
extern char                           * device_display_name;
extern char                             device_admin_password_hash[];
extern char                             device_normal_password_hash[];
extern char                             device_viewonly_password_hash[];
extern char                             device_config_name[];
extern uint32                           device_flags;
extern uint16                           device_tcp_port;
extern uint16                           device_provisioning_version;


ICACHE_FLASH_ATTR void                  device_load(uint8 *config_data);
ICACHE_FLASH_ATTR void                  device_save(uint8 *config_data, uint32 *strings_offs);


#endif /* _DEVICE_H */
