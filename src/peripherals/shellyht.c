
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

#include <c_types.h>

#include "espgoodies/common.h"
#include "espgoodies/drivers/gpio.h"
#include "espgoodies/drivers/hspi.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/shellyht.h"


#define SPI_BIT_ORDER HSPI_BIT_ORDER_MSB_FIRST
#define SPI_CPOL      1
#define SPI_CPHA      1
#define SPI_FREQ      200 * 1000 /* Hz */

#define FILL_BYTE          0xAA
#define HEADER_BYTE1       0xAA
#define HEADER_BYTE2       0x99
#define FOOTER_BYTE1       0xAA
#define FOOTER_BYTE2       0xAA
#define SLAVE_FOOTER_BYTE2 0x79

#define CMD_READ_MEASUREMENTS 0x01
#define CMD_LED               0x02
#define CMD_CONFIGURE         0x03
#define CMD_PREVENT_SLEEP     0x04

#define LED_STATUS_ON         0x01
#define LED_STATUS_OFF        0x02
#define LED_STATUS_BLINK_SLOW 0x03
#define LED_STATUS_BLINK_FAST 0x04

#define RESET_GPIO 5

#define MIN_SAMP_INT 1000    /* Milliseconds */
#define DEF_SAMP_INT 1000    /* Milliseconds */
#define MAX_SAMP_INT 3600000 /* Milliseconds */

#define MIN_TEMP -40 /* Degrees C */
#define MAX_TEMP 60  /* Degrees C */


typedef struct {

    double  last_temperature;
    double  last_humidity;
    int64   last_read_time_ms;
    bool    configured;

} user_data_t;


static bool   ICACHE_FLASH_ATTR  send_cmd_read_measurements(peripheral_t *peripheral);
static bool   ICACHE_FLASH_ATTR  send_cmd_led(peripheral_t *peripheral, uint8 led_status);
/*static bool   ICACHE_FLASH_ATTR  send_cmd_configure(
                                     peripheral_t *peripheral,
                                     uint8 temp_thresh,
                                     uint8 hum_thresh,
                                     uint16 samp_period
                                 );*/
static bool   ICACHE_FLASH_ATTR  send_cmd_prevent_sleep(peripheral_t *peripheral);
static bool   ICACHE_FLASH_ATTR  send_cmd_06(peripheral_t *peripheral);
static bool   ICACHE_FLASH_ATTR  send_cmd_07(peripheral_t *peripheral);

static bool   ICACHE_FLASH_ATTR  recv_measurements(peripheral_t *peripheral, double *temp, double *hum);
static void   ICACHE_FLASH_ATTR  recv_dummy(peripheral_t *peripheral, uint8 len);

static uint8  ICACHE_FLASH_ATTR *make_mosi_frame(uint8 cmd, uint8 *data, uint8 data_len, uint8 *frame_len);
static uint8  ICACHE_FLASH_ATTR *make_mosi_fill_frame(uint8 len);
static bool   ICACHE_FLASH_ATTR  miso_frame_valid(uint8 *frame, uint8 frame_len);

static bool   ICACHE_FLASH_ATTR  read_data(port_t *port);
static void   ICACHE_FLASH_ATTR  read_data_if_needed(port_t *port);

static void   ICACHE_FLASH_ATTR  configure(port_t *port, bool enabled);
static double ICACHE_FLASH_ATTR  read_temperature_value(port_t *port);
static double ICACHE_FLASH_ATTR  read_humidity_value(port_t *port);

static void   ICACHE_FLASH_ATTR  init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR  make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_shelly_ht = {

    .init = init,
    .make_ports = make_ports

};

bool send_cmd_read_measurements(peripheral_t *peripheral) {
    /* Frame format:
     *  AA 99 : header
     *  01    : cmd
     *  00    : len
     *  FE    : checksum
     *  AA AA : footer
     */

    DEBUG_SHELLY_HT(peripheral, "sending read-measurements command");

    uint8 frame_len;
    uint8 *mosi_frame = make_mosi_frame(CMD_READ_MEASUREMENTS, /* data = */ NULL, /* data_len = */ 0, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool send_cmd_led(peripheral_t *peripheral, uint8 led_status) {
    /* Frame format:
     *  AA 99 : header
     *  02    : cmd
     *  02    : len
     *  XX    : led_status
     *  04    : ?
     *  XX    : checksum
     *  AA AA : footer
     */

    DEBUG_SHELLY_HT(peripheral, "sending LED command with status=%02X", led_status);

    uint8 frame_len;
    uint8 data[] = {led_status, 0x04};
    uint8 *mosi_frame = make_mosi_frame(CMD_LED, data, sizeof(data), &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

//bool send_cmd_configure(peripheral_t *peripheral, uint8 temp_thresh, uint8 hum_thresh, uint16 samp_period) {
//    /* Frame format:
//     *  AA 99 : header
//     *  03    : cmd
//     *  05    : len
//     *  00    : ?
//     *  XX    : temp_thresh * 8 (e.g. 14: 2.5C)
//     *  XX    : hum_thresh * 2 (e.g. 0A: 5%)
//     *  XX XX : samp_period [minutes] (e.g. 02 D0: 720 minutes)
//     *  XX    : checksum
//     *  AA AA : footer
//     */
//
//    DEBUG_SHELLY_HT(peripheral,
//        "sending configure command with temp_thresh=%d, hum_thresh=%d, samp_period=%d",
//        temp_thresh,
//        hum_thresh,
//        samp_period
//    );
//
//    uint8 frame_len;
//    uint8 data[] = {0x00, temp_thresh * 8, hum_thresh * 2, (uint8) (samp_period >> 8), samp_period & 0xFF};
//    uint8 *mosi_frame = make_mosi_frame(CMD_CONFIGURE, data, sizeof(data), &frame_len);
//    uint8 miso_frame[frame_len];
//
//    hspi_transfer(mosi_frame, miso_frame, frame_len);
//    free(mosi_frame);
//
//    if (!miso_frame_valid(miso_frame, frame_len)) {
//        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
//        return FALSE;
//    }
//
//    return TRUE;
//}

bool send_cmd_prevent_sleep(peripheral_t *peripheral) {
    /* Frame format:
     *  AA 99 : header
     *  04    : cmd
     *  00    : len
     *  FB    : checksum
     *  AA AA : footer
     */

    DEBUG_SHELLY_HT(peripheral, "sending prevent-sleep command");

    uint8 frame_len;
    uint8 *mosi_frame = make_mosi_frame(CMD_PREVENT_SLEEP, /* data = */ NULL, /* data_len = */ 0, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool send_cmd_06(peripheral_t *peripheral) {
    /* Frame format:
     *  AA 99 : header
     *  06    : cmd
     *  00    : len
     *  F9    : checksum
     *  AA AA : footer
     */

    DEBUG_SHELLY_HT(peripheral, "sending command 06");

    uint8 frame_len;
    uint8 *mosi_frame = make_mosi_frame(0x06, /* data = */ NULL, /* data_len = */ 0, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool send_cmd_07(peripheral_t *peripheral) {
    /* Frame format:
     *  AA 99 : header
     *  07    : cmd
     *  05    : len
     *  00    : ?
     *  00    : ?
     *  00    : ?
     *  00    : ?
     *  00    : ?
     *  FD    : checksum
     *  AA AA : footer
     */

    DEBUG_SHELLY_HT(peripheral, "sending command 07");

    uint8 frame_len;
    uint8 data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    uint8 *mosi_frame = make_mosi_frame(0x06, data, /* data_len = */ 5, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool recv_measurements(peripheral_t *peripheral, double *temp, double *hum) {
    /* Frame format:
     *  XX : ?
     *  XX : ?
     *  XX : ?
     *  XX : ?
     *  XX : ?
     *  XX : ?
     *  XX : ?
     *  XX : temp_high * 8
     *  XX : temp_low * 8
     *  XX : ?
     *  XX : hum * 2 (e.g. 60: 48%)
     *  XX : checksum
     */

    DEBUG_SHELLY_HT(peripheral, "receiving measurements");

    uint8 *mosi_frame = make_mosi_fill_frame(12);
    uint8 miso_frame[12];
    uint8 i;

    hspi_transfer(mosi_frame, miso_frame, 12);
    free(mosi_frame);

    int16 temp8 = (miso_frame[7] << 8) + miso_frame[8];
    *temp = temp8 / 8.0;
    *hum = miso_frame[10] / 2.0;

    /* Verify checksum */
    uint8 checksum = miso_frame[0];
    for (i = 1; i < 11; i++) {
        checksum ^= miso_frame[i];
    }
    checksum = 0xFF - checksum;
    if (checksum != miso_frame[11]) {
        DEBUG_SHELLY_HT(peripheral, "invalid slave response");
        return FALSE;
    }

    return TRUE;
}

void recv_dummy(peripheral_t *peripheral, uint8 len) {
    DEBUG_SHELLY_HT(peripheral, "receiving %d dummy bytes", len);

    uint8 *mosi_frame = make_mosi_fill_frame(len);
    uint8 miso_frame[len];
    hspi_transfer(mosi_frame, miso_frame, len);
    free(mosi_frame);
}

uint8 *make_mosi_frame(uint8 cmd, uint8 *data, uint8 data_len, uint8 *frame_len) {
    *frame_len = 2 /* header */ + 1 /* cmd */ + 1 /* len */ + data_len + 1 /* checksum */ + 2 /* footer */;
    uint8 *frame = malloc(*frame_len);
    uint8 *b = frame;

    /* Header */
    *(b++) = HEADER_BYTE1;
    *(b++) = HEADER_BYTE2;

    /* Command */
    *(b++) = cmd;

    /* Data length */
    *(b++) = data_len;

    /* Data */
    memcpy(b, data, data_len);
    b += data_len;

    /* Checksum */
    uint8 i, checksum = cmd ^ data_len;
    for (i = 0; i < data_len; i++) {
        checksum ^= data[i];
    }
    *(b++) = 0xFF - checksum;

    /* Footer */
    *(b++) = FOOTER_BYTE1;
    *(b++) = FOOTER_BYTE2;

    return frame;
}

uint8 *make_mosi_fill_frame(uint8 len) {
    uint8 *frame = malloc(len);
    memset(frame, FILL_BYTE, len);

    return frame;
}

bool miso_frame_valid(uint8 *frame, uint8 frame_len) {
    if (frame[0] != FILL_BYTE || frame[frame_len - 1] != SLAVE_FOOTER_BYTE2) {
        return FALSE;
    }

    return TRUE;
}

bool read_data(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    /* Reset low-power MCU, so that it reads new measurements from sensor */
    DEBUG_SHELLY_HT(peripheral, "resetting low-power MCU");
    gpio_write_value(RESET_GPIO, FALSE);
    os_delay_us(1000);
    gpio_write_value(RESET_GPIO, TRUE);
    os_delay_us(100000); /* Allow 100ms for the MCU to get ready */

    /* Turn LED off */
    if (!send_cmd_led(peripheral, LED_STATUS_OFF)) {
        DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
    }

    /* Read measurements */
    if (!send_cmd_read_measurements(peripheral)) {
        return FALSE;
    }

    double temp, hum;
    if (!recv_measurements(peripheral, &temp, &hum)) {
        return FALSE;
    }

    DEBUG_SHELLY_HT(peripheral, "got measurements: temp=%s, hum=%s", dtostr(temp, -1), dtostr(hum, -1));

    user_data->last_temperature = temp;
    user_data->last_humidity = hum;

    /* Commands 06 and 07 are sent by the original FW, but effect/reason is unknown */
    if (!send_cmd_06(peripheral)) {
        return TRUE;
    }
    if (!send_cmd_07(peripheral)) {
        return TRUE;
    }

    recv_dummy(peripheral, 5);

    return TRUE;
}

void read_data_if_needed(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    uint64 now_ms = system_uptime_ms();
    uint64 delta = now_ms - user_data->last_read_time_ms;
    if (delta >= port->sampling_interval) {
        user_data->last_read_time_ms = now_ms;
        DEBUG_SHELLY_HT(peripheral, "data reading is needed");
        if (!read_data(port)) {
            user_data->last_temperature = UNDEFINED;
            user_data->last_humidity = UNDEFINED;
        }
    }
}


void configure(port_t *port, bool enabled) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    if (enabled) {
        /* Prevent multiple SPI setups */
        if (!user_data->configured) {
            DEBUG_SHELLY_HT(peripheral, "configuring SPI");
            hspi_setup(SPI_BIT_ORDER, SPI_CPOL, SPI_CPHA, SPI_FREQ);
            user_data->configured = TRUE;

            /* Prevent low power MCU-induced sleep */
            send_cmd_prevent_sleep(peripheral);
            recv_dummy(peripheral, 3);

            if (!send_cmd_led(peripheral, LED_STATUS_BLINK_SLOW)) {
                DEBUG_SHELLY_HT(peripheral, "invalid SPI slave response");
            }

            /* GPIO5 is used to reset the low-power MCU */
            gpio_configure_output(RESET_GPIO, /* initial = */ TRUE);

            /* Set initial values to UNDEFINED */
            user_data->last_temperature = UNDEFINED;
            user_data->last_humidity = UNDEFINED;
        }
    }
}

double read_temperature_value(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(port);

    return user_data->last_temperature;
}

double read_humidity_value(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(port);

    return user_data->last_humidity;
}

void init(peripheral_t *peripheral) {
    peripheral->user_data = zalloc(sizeof(user_data_t));
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    port_t *temperature_port = port_create();
    temperature_port->slot = -1;
    temperature_port->type = PORT_TYPE_NUMBER;
    temperature_port->min = MIN_TEMP;
    temperature_port->max = MAX_TEMP;
    temperature_port->unit = "C";
    temperature_port->min_sampling_interval = MIN_SAMP_INT;
    temperature_port->max_sampling_interval = MAX_SAMP_INT;
    temperature_port->def_sampling_interval = DEF_SAMP_INT;
    temperature_port->read_value = read_temperature_value;
    temperature_port->configure = configure;
    ports[(*ports_len)++] = temperature_port;

    port_t *humidity_port = port_create();
    humidity_port->slot = -1;
    humidity_port->type = PORT_TYPE_NUMBER;
    humidity_port->min = 0;
    humidity_port->max = 100;
    humidity_port->unit = "%";
    humidity_port->min_sampling_interval = MIN_SAMP_INT;
    humidity_port->max_sampling_interval = MAX_SAMP_INT;
    humidity_port->def_sampling_interval = DEF_SAMP_INT;
    humidity_port->read_value = read_humidity_value;
    humidity_port->configure = configure;
    ports[(*ports_len)++] = humidity_port;
}
