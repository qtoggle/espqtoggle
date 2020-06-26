
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

#include "espgoodies/crypto.h"

#include "api.h"
#include "common.h"
#include "device.h"


char                    device_name[API_MAX_DEVICE_NAME_LEN + 1] = {0};
char                    device_display_name[API_MAX_DEVICE_DISP_NAME_LEN + 1] = {0};
char                    device_admin_password_hash[SHA256_HEX_LEN + 1] = {0};
char                    device_normal_password_hash[SHA256_HEX_LEN + 1] = {0};
char                    device_viewonly_password_hash[SHA256_HEX_LEN + 1] = {0};
char                    device_config_name[API_MAX_DEVICE_CONFIG_NAME_LEN + 1] = {0};
uint32                  device_flags = 0;
uint16                  device_tcp_port = 0;
