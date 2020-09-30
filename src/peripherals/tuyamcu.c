
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
 * Based on https://developer.tuya.com/en/docs/iot/device-development/access-mode-mcu/wifi-general-solution/
 *          software-reference-wifi/tuya-cloud-universal-serial-port-access-protocol
 */

#include <stdlib.h>
#include <c_types.h>
#include <ets_sys.h>
#include <user_interface.h>
#include <limits.h>

#include "espgoodies/common.h"
#include "espgoodies/debug.h"
#include "espgoodies/drivers/uart.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"
#include "espgoodies/wifi.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/tuyamcu.h"


#define UART_NO        UART0
#define UART_BAUD      9600
#define UART_PARITY    UART_PARITY_NONE
#define UART_STOP_BITS UART_STOP_BITS_1
#define UART_BUFF_SIZE 1024

#define READ_TIMEOUT       50000 /* Microseconds */
#define WRITE_TIMEOUT      50000 /* Microseconds */
#define SYNC_TIMEOUT       1000   /* Milliseconds */
#define HEARTBEAT_INTERVAL 15000 /* Milliseconds */

#define FRAME_HEADER0 0x55
#define FRAME_HEADER1 0xAA

#define MIN_FRAME_LEN 7
#define MAX_DATA_LEN  (UART_BUFF_SIZE - MIN_FRAME_LEN)

#define CMD_HEARTBEAT         0x00
#define CMD_PROD_INFO         0x01
#define CMD_SETUP_TYPE        0x02
#define CMD_NET_STATUS_REPORT 0x03
#define CMD_RESET_SWITCH      0x04
#define CMD_RESET_CHANGE      0x05
#define CMD_DP_SET            0x06
#define CMD_DP_REPORT         0x07
#define CMD_DP_QUERY          0x08
#define CMD_START_OTA         0x0A
#define CMD_TRANSMIT_OTA      0x0B
#define CMD_SYSTEM_TIME       0x0C
#define CMD_WIFI_TEST         0x0E
#define CMD_MEM_INFO          0x0F
#define CMD_LOCAL_TIME        0x1C
#define CMD_WEATHER           0x20
#define CMD_DP_REPORT_SYNC    0x22
#define CMD_DP_CONFIRM_SYNC   0x23
#define CMD_WIFI_STRENGTH     0x24
#define CMD_NO_HEARTBEAT      0x25
#define CMD_MAP_DATA          0x28
#define CMD_WIFI_CONFIG       0x2A
#define CMD_NET_STATUS        0x2B
#define CMD_WIFI_TEST_CONN    0x2C
#define CMD_MAC_ADDR          0x2D
#define CMD_IR_STATUS         0x2E
#define CMD_MAP_DATA_STREAM   0x30
#define CMD_DOWNLOAD_START    0x31
#define CMD_DOWNLOAD_SEND     0x32
#define CMD_EXTENDED          0x34
#define CMD_VOICE_STATUS      0x60
#define CMD_VOICE_MUTE        0x61
#define CMD_VOICE_VOLUME      0x62
#define CMD_VOICE_AUDIO_TEST  0x63
#define CMD_VOICE_WAKEUP_TEST 0x64
#define CMD_VOICE_EXTENSION   0x65

#define NET_STATUS_SMART_CONFIG    0x00
#define NET_STATUS_AP              0x01
#define NET_STATUS_NOT_CONNECTED   0x02
#define NET_STATUS_CONNECTED       0x03
#define NET_STATUS_CONNECTED_CLOUD 0x04
#define NET_STATUS_LOW_POWER       0x05
#define NET_STATUS_SMART_CONFIG_AP 0x06

#define RESET_SMART_MODE 0x00
#define RESET_AP_MODE    0x01

#define FLAG_CONFIGURED   0
#define FLAG_LOW_POWER    1
#define FLAG_IR           2
#define FLAG_SELF_SETUP   3
#define FLAG_NO_HEARTBEAT 4

#define DP_TYPE_BOOLEAN 0x01
#define DP_TYPE_NUMBER  0x02
#define DP_TYPE_STRING  0x03
#define DP_TYPE_ENUM    0x04
#define DP_TYPE_FAULT   0x05

#define PARAM_DP_FLAG_TYPE_MASK 0x0F
#define PARAM_DP_FLAG_WRITABLE  4

#define FLAG_NO_FORCE_COORDINATED_SETUP 0
#define PARAM_NO_POLLING_INTERVAL       0


typedef struct {

    uint32 value;
    uint16 flags;
    uint8  dp_id;
    bool   valid;

} dp_details_t;

typedef struct {

    int64         last_heartbeat_time_ms;
    int64         last_polling_time_ms;
    dp_details_t *dp_details;
    uint8        *dp_details_pos_by_id;
    uint8         dp_count;
    uint8         dp_max_id;
    uint8         flags;
    uint8         ir_in_pin;
    uint8         ir_out_pin;
    bool          heartbeat_pending;
    bool          last_net_status;
    uint8         polling_interval;

} user_data_t;


static uint8 *ICACHE_FLASH_ATTR make_frame(
                                    peripheral_t *peripheral,
                                    uint8 cmd,
                                    uint8 *data,
                                    uint16 data_len,
                                    uint16 *frame_len
                                );
#define                         make_frame_empty(peri, cmd, frame_len) make_frame(peri, cmd, NULL, 0, frame_len)
static uint8 *ICACHE_FLASH_ATTR parse_frame(
                                    peripheral_t *peripheral,
                                    uint8 *frame,
                                    uint16 frame_len,
                                    uint8 *cmd,
                                    uint16 *data_len
                                );

static uint8 *ICACHE_FLASH_ATTR make_dp_frame_boolean(
                                    peripheral_t *peripheral,
                                    uint8 dp_id,
                                    bool value,
                                    uint16 *frame_len
                                );

static uint8 *ICACHE_FLASH_ATTR make_dp_frame_number(
                                    peripheral_t *peripheral,
                                    uint8 dp_id,
                                    uint32 value,
                                    uint16 *frame_len
                                );

//static uint8 *ICACHE_FLASH_ATTR make_dp_frame_string(
//                                    peripheral_t *peripheral,
//                                    uint8 dp_id,
//                                    char *value,
//                                    uint16 *frame_len
//                                );

static uint8 *ICACHE_FLASH_ATTR make_dp_frame_enum(
                                    peripheral_t *peripheral,
                                    uint8 dp_id,
                                    uint8 value,
                                    uint16 *frame_len
                                );

static bool   ICACHE_FLASH_ATTR parse_dp_frame(
                                    peripheral_t *peripheral,
                                    uint8 *frame,
                                    uint16 frame_len,
                                    uint8 *dp_id,
                                    uint8 *type,
                                    void *value,
                                    uint16 *value_len
                                );

static uint8 *ICACHE_FLASH_ATTR wait_uart_frame(
                                    peripheral_t *peripheral,
                                    uint16 *frame_len,
                                    uint32 timeout_ms
                                );
static bool   ICACHE_FLASH_ATTR wait_uart_cmd(
                                    peripheral_t *peripheral,
                                    uint8 cmd,
                                    uint8 **data,
                                    uint16 *data_len,
                                    uint32 timeout_ms
                                );

static uint8  ICACHE_FLASH_ATTR compute_checksum(uint8 *frame, uint16 frame_len);

static double ICACHE_FLASH_ATTR read_value(port_t *port);
static bool   ICACHE_FLASH_ATTR write_value(port_t *port, double value);
static void   ICACHE_FLASH_ATTR configure(port_t *port, bool enabled);

static void   ICACHE_FLASH_ATTR init_mcu(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR update(peripheral_t *peripheral);

static void   ICACHE_FLASH_ATTR process_next_frame(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR process_dp_report_cmd(
                                    peripheral_t *peripheral,
                                    uint8 *data,
                                    uint16 data_len,
                                    bool sync
                                );
static void   ICACHE_FLASH_ATTR process_system_time_cmd(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR process_local_time_cmd(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR process_mem_info_cmd(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR process_wifi_strength_cmd(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR process_mac_addr_cmd(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR process_ir_status_cmd(peripheral_t *peripheral, uint8 *data, uint16 data_len);
static bool   ICACHE_FLASH_ATTR update_dp_value(peripheral_t *peripheral, uint8 dp_id, uint8 type, uint32 value);
static uint8  ICACHE_FLASH_ATTR compute_net_status(void);
static void   ICACHE_FLASH_ATTR send_empty_cmd(peripheral_t *peripheral, uint8 cmd);
static void   ICACHE_FLASH_ATTR send_net_status_cmd(peripheral_t *peripheral, uint8 cmd);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR cleanup(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_tuya_mcu = {

    .init = init,
    .cleanup = cleanup,
    .make_ports = make_ports

};


uint8 *make_frame(peripheral_t *peripheral, uint8 cmd, uint8 *data, uint16 data_len, uint16 *frame_len) {
    *frame_len = 7 + data_len;
    uint8 *frame = malloc(*frame_len);

    frame[0] = FRAME_HEADER0;
    frame[1] = FRAME_HEADER1;
    frame[2] = 0; /* Version */
    frame[3] = cmd; /* Version */
    uint16 data_len_n = htons(data_len);
    memcpy(frame + 4, &data_len_n, 2);
    if (data_len > 0) {
        memcpy(frame + 6, data, data_len);
    }
    frame[*frame_len - 1] = compute_checksum(frame, *frame_len - 1);

    return frame;
}

uint8 *parse_frame(peripheral_t *peripheral, uint8 *frame, uint16 frame_len, uint8 *cmd, uint16 *data_len) {
    *cmd = 0xFF; /* A way to indicate error */

    if (frame_len < 7) {
        DEBUG_TUYA_MCU(peripheral, "invalid frame length");
        return NULL;
    }

    if (frame[0] != FRAME_HEADER0 || frame[1] != FRAME_HEADER1) {
        DEBUG_TUYA_MCU(peripheral, "invalid frame header");
        return NULL;
    }

    /* frame[2] represents version and we simply ignore it */

    uint8 checksum = compute_checksum(frame, frame_len - 1);
    if (checksum != frame[frame_len - 1]) {
        DEBUG_TUYA_MCU(peripheral, "invalid frame checksum");
        return NULL;
    }

    *cmd = frame[3];
    memcpy(data_len, frame + 4, 2);
    *data_len = ntohs(*data_len);
    uint8 *data = NULL;
    if (*data_len > 0) {
        data = malloc(*data_len);
        memcpy(data, frame + 6, *data_len);
    }

    return data;
}

uint8 *make_dp_frame_boolean(peripheral_t *peripheral, uint8 dp_id, bool value, uint16 *frame_len) {
    uint16 data_len = 1;
    *frame_len = 4 + data_len;

    uint8 *frame = malloc(*frame_len);

    frame[0] = dp_id;
    frame[1] = DP_TYPE_BOOLEAN;

    uint16 data_len_n = htons(data_len);
    memcpy(frame + 2, &data_len_n, 2);
    frame[4] = value;

    return frame;
}

uint8 *make_dp_frame_number(peripheral_t *peripheral, uint8 dp_id, uint32 value, uint16 *frame_len) {
    uint16 data_len = 4;
    *frame_len = 4 + data_len;

    uint8 *frame = malloc(*frame_len);

    frame[0] = dp_id;
    frame[1] = DP_TYPE_NUMBER;

    uint16 data_len_n = htons(data_len);
    memcpy(frame + 2, &data_len_n, 2);

    uint32 value_n = htonl(value);
    memcpy(frame + 4, &value_n, 4);

    return frame;
}

//uint8 *make_dp_frame_string(peripheral_t *peripheral, uint8 dp_id, char *value, uint16 *frame_len) {
//    uint16 data_len = strlen(value);
//    *frame_len = 4 + data_len;
//
//    uint8 *frame = malloc(*frame_len);
//
//    frame[0] = dp_id;
//    frame[1] = DP_TYPE_STRING;
//
//    uint16 data_len_n = htons(data_len);
//    memcpy(frame + 2, &data_len_n, 2);
//    memcpy(frame + 4, value, data_len);
//
//    return frame;
//}

uint8 *make_dp_frame_enum(peripheral_t *peripheral, uint8 dp_id, uint8 value, uint16 *frame_len) {
    uint16 data_len = 1;
    *frame_len = 4 + data_len;

    uint8 *frame = malloc(*frame_len);

    frame[0] = dp_id;
    frame[1] = DP_TYPE_ENUM;

    uint16 data_len_n = htons(data_len);
    memcpy(frame + 2, &data_len_n, 2);
    frame[4] = value;

    return frame;
}

bool parse_dp_frame(
    peripheral_t *peripheral,
    uint8 *frame,
    uint16 frame_len,
    uint8 *dp_id,
    uint8 *type,
    void *value,
    uint16 *value_len
) {
    if (frame_len < 5) {
        DEBUG_TUYA_MCU(peripheral, "invalid DP frame length");
        return FALSE;
    }

    *dp_id = frame[0];
    *type = frame[1];

    memcpy(value_len, frame + 2, 2);
    *value_len = ntohs(*value_len);

    if (*value_len + 4 > frame_len) {
        DEBUG_TUYA_MCU(peripheral, "invalid DP frame length");
        return FALSE;
    }

    switch (*type) {
        case DP_TYPE_BOOLEAN:
        case DP_TYPE_ENUM: {
            * (uint32 *) value = frame[4];
            break;
        }

        case DP_TYPE_NUMBER: {
            uint32 value_n;
            memcpy(&value_n, frame + 4, 4);
            * ((uint32 *) value) = ntohl(value_n);
            break;
        }

        case DP_TYPE_STRING: {
            char **s = (char **) value;
            *s = malloc(*value_len + 1);
            memcpy(*s, frame + 2, *value_len);
            *s[*value_len] = 0; /* Add null terminator */
            break;
        }
    }

    return TRUE;
}

uint8 *wait_uart_frame(peripheral_t *peripheral, uint16 *frame_len, uint32 timeout_ms) {
    uint8 *frame = malloc(MIN_FRAME_LEN);
    uint16 data_len = 0;
    bool instant = timeout_ms == 0;

    while (timeout_ms > 0 || instant) {
        if (!instant) {
            os_delay_us(1000);
            timeout_ms--;
        }

        if (uart_buff_avail(UART_NO) < MIN_FRAME_LEN) {
            if (instant) {
                free(frame); return NULL;
            }
            continue;
        }

        /* Look for frame header */
        uart_buff_peek(UART_NO, frame, 7);
        if (frame[0] != FRAME_HEADER0 || frame[1] != FRAME_HEADER1) {
            uart_buff_read(UART_NO, frame, 1); /* Skip one byte */
            if (instant) {
                free(frame); return NULL;
            }
            continue;
        }

        /* Read data length of our potential frame */
        data_len = ntohs(* (uint16 *) (frame + 4));
        if (data_len > MAX_DATA_LEN) { /* Ignore unreasonably large frames */
            uart_buff_read(UART_NO, frame, 2); /* Skip header */
            if (instant) {
                free(frame); return NULL;
            }
            continue;
        }

        break;
    }

    if (!timeout_ms && !instant) { /* No frame found within given timeout */
        free(frame);
        return NULL;
    }

    /* We now actually need to read frame data we've got so far */

    if (!data_len) { /* No data, all frame content is already buffered */
        uart_buff_read(UART_NO, frame, MIN_FRAME_LEN);
        *frame_len = MIN_FRAME_LEN;
        return frame;
    }

    /* Wait for the entire frame content to be buffered */
    *frame_len = MIN_FRAME_LEN + data_len;
    if (instant) {
        if (uart_buff_avail(UART_NO) < *frame_len) {
            free(frame); return NULL;
        }
    }
    else {
        while (timeout_ms > 0 && uart_buff_avail(UART_NO) < *frame_len) {
            os_delay_us(1000);
            timeout_ms--;
        }
    }

    if (!timeout_ms && !instant) { /* Frame could not be received within given timeout */
        free(frame);
        return NULL;
    }

    frame = realloc(frame, *frame_len);
    uart_buff_read(UART_NO, frame, *frame_len);

    return frame;
}

bool wait_uart_cmd(peripheral_t *peripheral, uint8 cmd, uint8 **data, uint16 *data_len, uint32 timeout_ms) {
    uint8 *frame, *d, c;
    uint16 frame_len, dl;

    /* Wait up to timeout for frame containing our command */
    uint32 count = 1000 * timeout_ms / READ_TIMEOUT;
    while (count-- > 0) {
        frame = wait_uart_frame(peripheral, &frame_len, READ_TIMEOUT / 1000);
        if (!frame) {
            continue;
        }

        d = parse_frame(peripheral, frame, frame_len, &c, &dl);
        free(frame);

        if (c != cmd) {
            if (d) {
                free(d);
            }
            continue;
        }

        if (data) {
            *data = d;
        }
        if (data_len) {
            *data_len = dl;
        }

        return TRUE;
    }

    return FALSE;
}

uint8 compute_checksum(uint8 *frame, uint16 frame_len) {
    uint8 sum = 0;
    for (int i = 0; i < frame_len; i++) {
        sum += frame[i];
    }

    return sum;
}

double read_value(port_t *port) {
    peripheral_t *peripheral = port->peripheral;

    update(peripheral);

    dp_details_t *dp_details = port->user_data;
    if (!dp_details->valid) {
        return UNDEFINED;
    }

    return dp_details->value;
}

bool write_value(port_t *port, double value) {
    uint8 *data;
    uint16 data_len;
    dp_details_t *dp_details = port->user_data;
    uint8 type = dp_details->flags & PARAM_DP_FLAG_TYPE_MASK;
    peripheral_t *peripheral = port->peripheral;

    if (type == DP_TYPE_BOOLEAN) {
        data = make_dp_frame_boolean(peripheral, dp_details->dp_id, (bool) value, &data_len);
    }
    else if (type == DP_TYPE_NUMBER) {
        data = make_dp_frame_number(peripheral, dp_details->dp_id, (uint32) value, &data_len);
    }
    else if (type == DP_TYPE_ENUM) {
        data = make_dp_frame_enum(peripheral, dp_details->dp_id, (uint8) value, &data_len);
    }
    else {
        DEBUG_TUYA_MCU(peripheral, "unsupported data type 0x%0X", type);
        return FALSE;
    }

    uint16 frame_len;
    uint8 *frame = make_frame(port->peripheral, CMD_DP_SET, data, data_len, &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
        return FALSE;
    }

    return TRUE;
}

void configure(port_t *port, bool enabled) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    /* Configure serial port once, upon enabling first port */
    if (enabled && !(user_data->flags & BIT(FLAG_CONFIGURED))) {
#ifdef _DEBUG
        /* Switch debug to second UART */
        if (debug_uart_get_no() == UART_NO) {
            debug_uart_setup(1 - UART_NO);
        }
#endif
        DEBUG_TUYA_MCU(peripheral, "configuring serial port");
        uart_setup(UART_NO, UART_BAUD, UART_PARITY, UART_STOP_BITS);
        uart_buff_setup(UART_NO, UART_BUFF_SIZE);
        user_data->flags |= BIT(FLAG_CONFIGURED);

        DEBUG_TUYA_MCU(peripheral, "initializing MCU");
        init_mcu(peripheral);
    }
}

void init_mcu(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    uint16 frame_len, data_len, size;
    uint8 *frame = make_frame_empty(peripheral, CMD_HEARTBEAT, &frame_len);
    uint8 *data;

    /* Send heartbeat detection */
    DEBUG_TUYA_MCU(peripheral, "detecting initial heartbeat");
    send_empty_cmd(peripheral, CMD_HEARTBEAT);
    user_data->last_heartbeat_time_ms = system_uptime_ms();

    if (wait_uart_cmd(peripheral, CMD_HEARTBEAT, NULL, NULL, SYNC_TIMEOUT)) {
        DEBUG_TUYA_MCU(peripheral, "got initial heartbeat");
    }
    else {
        DEBUG_TUYA_MCU(peripheral, "timeout waiting for initial heartbeat");
    }

    /* Query product info */
    DEBUG_TUYA_MCU(peripheral, "querying product info");
    frame = make_frame_empty(peripheral, CMD_PROD_INFO, &frame_len);
    size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }

    if (wait_uart_cmd(peripheral, CMD_PROD_INFO, &data, &data_len, SYNC_TIMEOUT)) {
        DEBUG_TUYA_MCU(peripheral, "got product info");
        char *json_data = strndup((char *) data, data_len);
        json_t *json = json_parse(json_data);
        free(data);
        if (json) {
            if (json_get_type(json) == JSON_TYPE_OBJ) {
                json_t *ir_json = json_obj_lookup_key(json, "ir"); /* e.g. "5.12": output - GPIO5, input - GPIO12 */
                if (ir_json && json_get_type(ir_json) == JSON_TYPE_STR) {
                    char *ir = json_str_get(ir_json);
                    char *out_str = ir;
                    char *in_str = ir;
                    while (*in_str++) {
                        if (*in_str == '.') {
                            *in_str = '\0';
                            break;
                        }
                    }

                    user_data->ir_out_pin = strtol(out_str, NULL, 10);
                    user_data->ir_in_pin = strtol(in_str, NULL, 10);
                    user_data->flags |= BIT(FLAG_IR);

                    DEBUG_TUYA_MCU(peripheral, "IR pins: in = %d, out = %d", user_data->ir_in_pin, user_data->ir_out_pin);
                }

                json_t *low_json = json_obj_lookup_key(json, "low"); /* 0/1 */
                if (low_json && json_get_type(low_json) == JSON_TYPE_INT && json_int_get(low_json) == 1) {
                    user_data->flags |= BIT(FLAG_LOW_POWER);
                }
            }
            json_free(json);
        }
        else {
            DEBUG_TUYA_MCU(peripheral, "failed o parse product info");
        }
    }
    else {
        DEBUG_TUYA_MCU(peripheral, "timeout waiting for product info");
    }

    /* Query setup type */
    DEBUG_TUYA_MCU(peripheral, "querying setup type");
    frame = make_frame_empty(peripheral, CMD_SETUP_TYPE, &frame_len);
    size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }

    if (wait_uart_cmd(peripheral, CMD_SETUP_TYPE, &data, &data_len, SYNC_TIMEOUT)) {
        if (data_len >= 2) {
            user_data->flags |= BIT(FLAG_SELF_SETUP);

            DEBUG_TUYA_MCU(peripheral, "self setup type: status = %d, reset = %d", data[0], data[1]);
        }
        else {
            DEBUG_TUYA_MCU(peripheral, "coordinated setup type");
        }
        free(data);
    }
    else {
        DEBUG_TUYA_MCU(peripheral, "timeout waiting for setup type");
    }

    if (PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_FORCE_COORDINATED_SETUP)) {
        DEBUG_TUYA_MCU(peripheral, "force coordinated setup");
        user_data->flags &= ~BIT(FLAG_SELF_SETUP);
    }

    /* Send initial net status, unless self-setup */
    if (!(user_data->flags & BIT(FLAG_SELF_SETUP))) {
        DEBUG_TUYA_MCU(peripheral, "sending initial net status");
        send_net_status_cmd(peripheral, CMD_NET_STATUS_REPORT);
    }

    /* Query initial DP values */
    DEBUG_TUYA_MCU(peripheral, "querying initial DP values");
    send_empty_cmd(peripheral, CMD_DP_QUERY);
    user_data->last_polling_time_ms = system_uptime_ms();

    /* Parsing of DPs will be done during one of the following update() calls */
}

void process_next_frame(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    uint8 *frame, *data, cmd;
    uint16 frame_len, data_len;

    frame = wait_uart_frame(peripheral, &frame_len, /* timeout = */ 0);
    if (!frame) {
        return;
    }

    data = parse_frame(peripheral, frame, frame_len, &cmd, &data_len);
    free(frame);

    switch (cmd) {
        case CMD_HEARTBEAT:
            DEBUG_TUYA_MCU(peripheral, "got HEARTBEAT command");
            user_data->heartbeat_pending = FALSE;
            break;

        case CMD_RESET_SWITCH:
        case CMD_RESET_CHANGE:
            /* Toggle setup mode */
            DEBUG_TUYA_MCU(peripheral, "got RESET command");
            system_setup_mode_toggle();
            send_empty_cmd(peripheral, cmd);
            break;

        case CMD_DP_REPORT:
            DEBUG_TUYA_MCU(peripheral, "got DP_REPORT command");
            process_dp_report_cmd(peripheral, data, data_len, /* sync = */ FALSE);
            break;

        case CMD_DP_REPORT_SYNC:
            DEBUG_TUYA_MCU(peripheral, "got DP_REPORT_SYNC command");
            process_dp_report_cmd(peripheral, data, data_len, /* sync = */ TRUE);
            break;

        case CMD_NO_HEARTBEAT:
            DEBUG_TUYA_MCU(peripheral, "got NO_HEARTBEAT command");
            user_data->flags |= BIT(FLAG_NO_HEARTBEAT);
            send_empty_cmd(peripheral, CMD_NO_HEARTBEAT);
            break;

        case CMD_LOCAL_TIME:
            DEBUG_TUYA_MCU(peripheral, "got LOCAL_TIME command");
            process_local_time_cmd(peripheral);
            break;

        case CMD_SYSTEM_TIME:
            DEBUG_TUYA_MCU(peripheral, "got SYSTEM_TIME command");
            process_system_time_cmd(peripheral);
            break;

        case CMD_MEM_INFO:
            DEBUG_TUYA_MCU(peripheral, "got MEM_INFO command");
            process_mem_info_cmd(peripheral);
            break;

        case CMD_WIFI_STRENGTH:
            DEBUG_TUYA_MCU(peripheral, "got WIFI_STRENGTH command");
            process_wifi_strength_cmd(peripheral);
            break;

        case CMD_NET_STATUS:
            DEBUG_TUYA_MCU(peripheral, "got NET_STATUS command");
            send_net_status_cmd(peripheral, cmd);
            break;

        case CMD_NET_STATUS_REPORT:
            DEBUG_TUYA_MCU(peripheral, "got NET_STATUS_REPORT command");
            break;

        case CMD_MAC_ADDR:
            DEBUG_TUYA_MCU(peripheral, "got MAC_ADDR command");
            process_mac_addr_cmd(peripheral);
            break;

        case CMD_IR_STATUS:
            DEBUG_TUYA_MCU(peripheral, "got IR_STATUS command");
            process_ir_status_cmd(peripheral, data, data_len);
            break;

        default:
            DEBUG_TUYA_MCU(peripheral, "ignoring received command 0x%02X", cmd);
    }

    free(data);
}

void update(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    process_next_frame(peripheral);

    /* Detect heartbeat timeouts */
    uint64 now_ms = system_uptime_ms();
    if (now_ms - user_data->last_heartbeat_time_ms > HEARTBEAT_INTERVAL) {
        DEBUG_TUYA_MCU(peripheral, "sending HEARTBEAT command");
        send_empty_cmd(peripheral, CMD_HEARTBEAT);
        user_data->last_heartbeat_time_ms = now_ms;
        user_data->heartbeat_pending = TRUE;
    }

    if (user_data->heartbeat_pending && now_ms - user_data->last_heartbeat_time_ms > SYNC_TIMEOUT) {
        DEBUG_TUYA_MCU(peripheral, "heartbeat timeout");
        user_data->heartbeat_pending = FALSE;

        /* Invalidate all DP values */
        for (int i = 0; i < user_data->dp_count; i++) {
            user_data->dp_details[i].valid = FALSE;
        }
    }

    /* Send setup mode updates to MCU */
    if (!(user_data->flags & BIT(FLAG_SELF_SETUP))) {
        uint8 net_status = compute_net_status();
        if (net_status != user_data->last_net_status) {
            user_data->last_net_status = net_status;
            DEBUG_TUYA_MCU(peripheral, "sending changed net status");
            send_net_status_cmd(peripheral, CMD_NET_STATUS_REPORT);
        }
    }

    /* Poll for DP values, if polling enabled */
    if (user_data->polling_interval) {
        if (now_ms - user_data->last_polling_time_ms > user_data->polling_interval * 1000) {
            DEBUG_TUYA_MCU(peripheral, "polling DP values");
            send_empty_cmd(peripheral, CMD_DP_QUERY);
            user_data->last_polling_time_ms = now_ms;
        }
    }
}

void process_dp_report_cmd(peripheral_t *peripheral, uint8 *data, uint16 data_len, bool sync) {
    uint16 value_len, len_so_far = 0;
    uint8 dp_id;
    uint8 type;
    uint32 value;

    while (len_so_far < data_len) {
        if (!parse_dp_frame(peripheral, data + len_so_far, data_len - len_so_far, &dp_id, &type, &value, &value_len)) {
            break;
        }

        len_so_far += value_len + 4; /* value length + header length */

        update_dp_value(peripheral, dp_id, type, value);

        if (type == DP_TYPE_STRING) { /* String values must be freed */
            free((void *) value);
        }
    }

    if (sync) {
        /* Normally, sync DP report should be considered successfully processed only when the new value has reached
         * the cloud (or the hub or any client, in our case). But at this point we don't have any means to detect that,
         * so we simply consider it processed and send the confirmation to the MCU. */

        uint16 frame_len;
        uint8 confirmed = (uint8) wifi_station_is_connected();
        uint8 *frame = make_frame(peripheral, CMD_DP_CONFIRM_SYNC, &confirmed, 1, &frame_len);
        uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
        free(frame);
        if (size != frame_len) {
            DEBUG_TUYA_MCU(peripheral, "failed to write frame");
        }
    }
}

void process_system_time_cmd(peripheral_t *peripheral) {
    /* TODO: properly implement this command once we have real date/time */
    uint8 data[7]; memset(data, 0, sizeof(data));
    uint16 frame_len;

    uint8 *frame = make_frame(peripheral, CMD_SYSTEM_TIME, data, sizeof(data), &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void process_local_time_cmd(peripheral_t *peripheral) {
    /* TODO: properly implement this command once we have real date/time */
    uint8 data[7]; memset(data, 0, sizeof(data));
    uint16 frame_len;

    uint8 *frame = make_frame(peripheral, CMD_LOCAL_TIME, data, sizeof(data), &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void process_mem_info_cmd(peripheral_t *peripheral) {
    uint16 frame_len;
    uint32 avail_mem = htonl(system_get_free_heap_size());
    uint8 *frame = make_frame(peripheral, CMD_MEM_INFO, (uint8 *) &avail_mem, 4, &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void process_wifi_strength_cmd(peripheral_t *peripheral) {
    uint16 frame_len;

    int8 rssi = wifi_station_get_rssi();
    if (rssi < -100) {
        rssi = -100;
    }
    if (rssi > -30) {
        rssi = -30;
    }

    uint8 *frame = make_frame(peripheral, CMD_WIFI_STRENGTH, (uint8 *) &rssi, 1, &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void process_mac_addr_cmd(peripheral_t *peripheral) {
    uint8 mac_addr[7] = {0};
    wifi_get_macaddr(STATION_IF, mac_addr + 1);

    uint16 frame_len;
    uint8 *frame = make_frame(peripheral, CMD_MAC_ADDR, mac_addr, 7, &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void process_ir_status_cmd(peripheral_t *peripheral, uint8 *data, uint16 data_len) {
    // TODO: implement me once we have IR support
}

bool update_dp_value(peripheral_t *peripheral, uint8 dp_id, uint8 type, uint32 value) {
    user_data_t *user_data = peripheral->user_data;

    if (dp_id > user_data->dp_max_id) {
        DEBUG_TUYA_MCU(peripheral, "ignoring unconfigured DP ID %d", dp_id);
        return FALSE;
    }

    uint8 pos = user_data->dp_details_pos_by_id[dp_id];
    if (pos == 0xFF) { /* Unconfigured DP position */
        DEBUG_TUYA_MCU(peripheral, "ignoring unconfigured DP ID %d", dp_id);
        return FALSE;
    }

    if ((type != DP_TYPE_BOOLEAN) && (type != DP_TYPE_NUMBER) && (type != DP_TYPE_ENUM)) {
        DEBUG_TUYA_MCU(peripheral, "ignoring DP ID %d with unsupported type 0x%0X", dp_id, type);
        return FALSE;
    }

    dp_details_t *dp_details = user_data->dp_details + pos;
    dp_details->valid = TRUE;
    dp_details->value = value;

    DEBUG_TUYA_MCU(peripheral, "value for DP ID %d is %08X", dp_id, value);

    return TRUE;
}

uint8 compute_net_status(void) {
    if (system_setup_mode_active()) {
        return NET_STATUS_SMART_CONFIG;
    }
    else if (wifi_station_is_connected()) {
        return NET_STATUS_CONNECTED_CLOUD;
    }
    else {
        return NET_STATUS_AP; /* This will make the LED blink slowly, even though we're not actually in AP mode */
    }
}

void send_empty_cmd(peripheral_t *peripheral, uint8 cmd) {
    uint16 frame_len;
    uint8 *frame = make_frame_empty(peripheral, cmd, &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void send_net_status_cmd(peripheral_t *peripheral, uint8 cmd) {
    uint8 status = compute_net_status();
    uint16 frame_len;
    uint8 *frame = make_frame(peripheral, cmd, &status, 1, &frame_len);
    uint16 size = uart_write(UART_NO, frame, frame_len, WRITE_TIMEOUT);
    free(frame);
    if (size != frame_len) {
        DEBUG_TUYA_MCU(peripheral, "failed to write frame");
    }
}

void init(peripheral_t *peripheral) {
    user_data_t *user_data = zalloc(sizeof(user_data_t));

    peripheral->user_data = user_data;
    user_data->last_net_status = compute_net_status();
    user_data->polling_interval = PERIPHERAL_PARAM_UINT8(peripheral, PARAM_NO_POLLING_INTERVAL);
}

void cleanup(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    free(user_data->dp_details);
    free(user_data->dp_details_pos_by_id);
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    user_data_t *user_data = peripheral->user_data;

    /* First 8 bytes of params are to be interpreted as follows:
     *  * Byte 0: polling interval (seconds); 0 disables polling
     *
     * Following 48 bytes are organized as a list of DP details; for each DP, a port will be allocated, according to
     * given details. Details are structured as follows:
     *  * Byte  0:     DP_ID
     *  * Byte  1:     Reserved
     *  * Bytes 2-3:   16-bit flags (see below)
     *
     * List of details ends at first DP_ID set to 0.
     *
     * Flags:
     *  * Bit 0 + 1: port type
     *  * Bit 2: reserved
     *  * Bit 3: reserved
     *  * Bit 4: writable
     */

    uint8 *p = peripheral->params + 8;
    uint8 dp_count = 0;
    uint8 dp_max_id = 0;
    while (p - peripheral->params < PERIPHERAL_PARAMS_SIZE) {
        uint8 dp_id = p[0];
        if (dp_id == 0) {
            break; /* end of list */
        }

        p += 2;
        dp_count++;
        dp_max_id = MAX(dp_max_id, dp_id);

        port_t *port = port_create();
        user_data->dp_details = realloc(user_data->dp_details, sizeof(dp_details_t) * dp_count);
        dp_details_t *dp_details = &user_data->dp_details[dp_count - 1];

        dp_details->value = 0;
        dp_details->valid = FALSE;
        dp_details->flags = *(uint16 *) p; p += 2;
        dp_details->flags = ntohs(dp_details->flags);
        dp_details->dp_id = dp_id;

        /* type */
        uint8 type = dp_details->flags & PARAM_DP_FLAG_TYPE_MASK;
        switch (type) {
            case DP_TYPE_BOOLEAN:
                port->type = PORT_TYPE_BOOLEAN;
                break;

            case DP_TYPE_NUMBER:
            case DP_TYPE_ENUM:
                port->type = PORT_TYPE_NUMBER;
                break;

            default:
                free(port);
                continue;
        }

        /* writable */
        if (dp_details->flags & BIT(PARAM_DP_FLAG_WRITABLE)) {
            port->flags |= PORT_FLAG_WRITABLE;
        }

        port->slot = -1;
        port->read_value = read_value;
        port->write_value = write_value;
        port->configure = configure;
        ports[(*ports_len)++] = port;
    }

    /* Prepare DP details position by ID */
    user_data->dp_count = dp_count;
    user_data->dp_max_id = dp_max_id;
    user_data->dp_details_pos_by_id = malloc(dp_max_id + 1);
    memset(user_data->dp_details_pos_by_id, 0xFF, dp_max_id + 1); /* 0xFF indicates DP_ID not configured */
    for (int i = 0; i < dp_count; i++) {
        ports[i]->user_data = user_data->dp_details + i;
        user_data->dp_details_pos_by_id[user_data->dp_details[i].dp_id] = i;
    }
}
