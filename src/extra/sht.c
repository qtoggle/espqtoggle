
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
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <mem.h>
#include <user_interface.h>
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/gpio.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "drivers/hspi.h"
#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "ports.h"
#include "extra/sht.h"


#ifdef HAS_SHT

#define SPI_BIT_ORDER                   HSPI_BIT_ORDER_MSB_FIRST
#define SPI_CPOL                        1
#define SPI_CPHA                        1
#define SPI_FREQ                        200 * 1000 /* Hz */

#define MIN_SAMP_INT                    1000    /* Milliseconds */
#define DEF_SAMP_INT                    5000    /* Milliseconds */
#define MAX_SAMP_INT                    3600000 /* Milliseconds */

#define FILL_BYTE                       0xAA
#define HEADER_BYTE1                    0xAA
#define HEADER_BYTE2                    0x99
#define FOOTER_BYTE1                    0xAA
#define FOOTER_BYTE2                    0xAA
#define SLAVE_FOOTER_BYTE2              0x79

#define CMD_READ_MEASUREMENTS           0x01
#define CMD_LED                         0x02
#define CMD_CONFIGURE                   0x03
#define CMD_PREVENT_SLEEP               0x04

#define LED_STATUS_ON                   0x01
#define LED_STATUS_OFF                  0x02
#define LED_STATUS_BLINK_SLOW           0x03
#define LED_STATUS_BLINK_FAST           0x04

#define RESET_GPIO                      5


typedef struct {

    double                              last_temperature;
    double                              last_humidity;
    uint64                              last_read_time; /* Milliseconds */
    bool                                configured;
    bool                                sleep_prevented;

} extra_info_t;


ICACHE_FLASH_ATTR static void           configure(port_t *port);

#ifdef HAS_SHT_TEMPERATURE
ICACHE_FLASH_ATTR static double         read_temperature(port_t *port);
#endif

#ifdef HAS_SHT_HUMIDITY
ICACHE_FLASH_ATTR static double         read_humidity(port_t *port);
#endif


ICACHE_FLASH_ATTR static bool           read_data_if_needed(port_t *port);
ICACHE_FLASH_ATTR static bool           read_data(port_t *port);

ICACHE_FLASH_ATTR static bool           send_cmd_read_measurements(void);
ICACHE_FLASH_ATTR static bool           send_cmd_led(uint8 led_status);
/*
ICACHE_FLASH_ATTR static bool           send_cmd_configure(uint8 temp_thresh, uint8 hum_thresh, uint16 samp_period);
 */
ICACHE_FLASH_ATTR static bool           send_cmd_prevent_sleep(void);

ICACHE_FLASH_ATTR static bool           send_cmd_06(void);
ICACHE_FLASH_ATTR static bool           send_cmd_07(void);

ICACHE_FLASH_ATTR static bool           recv_measurements(double *temp, double *hum);
ICACHE_FLASH_ATTR static void           recv_dummy(uint8 len);

ICACHE_FLASH_ATTR static uint8        * make_mosi_frame(uint8 cmd, uint8 *data, uint8 data_len, uint8 *frame_len);
ICACHE_FLASH_ATTR static uint8        * make_mosi_fill_frame(uint8 len);
ICACHE_FLASH_ATTR static bool           miso_frame_valid(uint8 *frame, uint8 frame_len);


static extra_info_t                     sht_extra_info;


#ifdef HAS_SHT_TEMPERATURE

static port_t _sht_temperature = {

    .slot = PORT_SLOT_AUTO,

    .id = SHT_TEMPERATURE_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    /* We need to enable the port by default, so that it starts feeding the MCU upon read, or otherwise it goes into
     * sleep mode within a few minutes */
    .flags = PORT_FLAG_ENABLED,

    .extra_info = &sht_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_temperature,
    .configure = configure

};

port_t *sht_temperature = &_sht_temperature;

#endif

#ifdef HAS_SHT_HUMIDITY

static port_t _sht_humidity = {

    .slot = PORT_SLOT_AUTO,

    .id = SHT_HUMIDITY_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &sht_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_humidity,
    .configure = configure

};

port_t *sht_humidity = &_sht_humidity;

#endif


void configure(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    /* Prevent multiple setups */
    if (!extra_info->configured) {
        DEBUG_SHT("configuring SPI");
        hspi_setup(SPI_BIT_ORDER, SPI_CPOL, SPI_CPHA, SPI_FREQ);
        extra_info->configured = TRUE;

        /* Prevent sleep */
        extra_info->sleep_prevented = send_cmd_prevent_sleep();
        recv_dummy(3);

        if (!send_cmd_led(LED_STATUS_BLINK_SLOW)) {
            DEBUG_SHT("invalid SPI slave response");
        }

        /* GPIO5 is used to reset the low-power MCU */
        gpio_configure_output(RESET_GPIO, /* initial = */ TRUE);

        /* Set initial values to UNDEFINED */
        extra_info->last_temperature = UNDEFINED;
        extra_info->last_humidity = UNDEFINED;
    }
}

#ifdef HAS_SHT_TEMPERATURE

double read_temperature(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_data_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_temperature;
}

#endif

#ifdef HAS_SHT_HUMIDITY

double read_humidity(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_data_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_humidity;
}

#endif

bool read_data_if_needed(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    /* Prevent sleep mode, if not already prevented */
    if (!extra_info->sleep_prevented) {
        extra_info->sleep_prevented = send_cmd_prevent_sleep();
        recv_dummy(3);
    }

    uint64 now_ms = system_uptime_ms();
    uint64 delta = now_ms - extra_info->last_read_time;
    if (delta >= port->sampling_interval - 10) { /* Allow 10 milliseconds of tolerance */
        DEBUG_SHT("data needs new reading");

        /* Update last read time */
        extra_info->last_read_time = now_ms;

        if (read_data(port)) {
            return TRUE;
        }

        DEBUG_SHT("data reading failed");

        /* In case of error, cached status can be used within up to twice the sampling interval. */
        if (delta > port->sampling_interval * 2) {
            return FALSE;
        }
    }

    return TRUE;
}

bool read_data(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    /* Reset low-power MCU, so that it reads new measurements from sensor */
    DEBUG_SHT("resetting low-power MCU");
    gpio_write_value(RESET_GPIO, FALSE);
    os_delay_us(1000);
    gpio_write_value(RESET_GPIO, TRUE);
    os_delay_us(100000); /* Allow 100ms for the MCU to get ready */

    /* Turn LED off */
    if (!send_cmd_led(LED_STATUS_OFF)) {
        DEBUG_SHT("invalid SPI slave response");
    }

    /* Read measurements */
    if (!send_cmd_read_measurements()) {
        return FALSE;
    }

    double temp, hum;
    if (!recv_measurements(&temp, &hum)) {
        return FALSE;
    }

    DEBUG_SHT("got measurements: temp=%s, hum=%s", dtostr(temp, -1), dtostr(hum, -1));

    extra_info->last_temperature = temp;
    extra_info->last_humidity = hum;

    /* Commands 06 and 07 re sent by the original FW, but effect/reason is unknown. */
    if (!send_cmd_06()) {
        return TRUE;
    }
    if (!send_cmd_07()) {
        return TRUE;
    }

    recv_dummy(5);

    return TRUE;
}

bool send_cmd_read_measurements(void) {
    /* Frame format:
     *  AA 99 : header
     *  01    : cmd
     *  00    : len
     *  FE    : checksum
     *  AA AA : footer
     */

    DEBUG_SHT("sending read-measurements command");

    uint8 frame_len;
    uint8 *mosi_frame = make_mosi_frame(CMD_READ_MEASUREMENTS, /* data = */ NULL, /* data_len = */ 0, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHT("invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool send_cmd_led(uint8 led_status) {
    /* Frame format:
     *  AA 99 : header
     *  02    : cmd
     *  02    : len
     *  XX    : led_status
     *  04    : ?
     *  XX    : checksum
     *  AA AA : footer
     */

    DEBUG_SHT("sending LED command with status=%02X", led_status);

    uint8 frame_len;
    uint8 data[] = {led_status, 0x04};
    uint8 *mosi_frame = make_mosi_frame(CMD_LED, data, sizeof(data), &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHT("invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

//bool send_cmd_configure(uint8 temp_thresh, uint8 hum_thresh, uint16 samp_period) {
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
//    DEBUG_SHT("sending configure command with temp_thresh=%d, hum_thresh=%d, samp_period=%d",
//              temp_thresh, hum_thresh, samp_period);
//
//    uint8 frame_len;
//    uint8 data[] = {0x00, temp_thresh * 8, hum_thresh * 2, (uint8) (samp_period >> 8), samp_period & 0xFF};
//    uint8 *mosi_frame = make_mosi_frame(CMD_CONFIGURE, data, sizeof(data), &frame_len);
//    uint8 miso_frame[frame_len];
//
//    spi_transfer(mosi_frame, miso_frame, frame_len);
//    free(mosi_frame);
//
//    if (!miso_frame_valid(miso_frame, frame_len)) {
//        DEBUG_SHT("invalid SPI slave response");
//        return FALSE;
//    }
//
//    return TRUE;
//}

bool send_cmd_prevent_sleep(void) {
    /* Frame format:
     *  AA 99 : header
     *  04    : cmd
     *  00    : len
     *  FB    : checksum
     *  AA AA : footer
     */

    DEBUG_SHT("sending prevent-sleep command");

    uint8 frame_len;
    uint8 *mosi_frame = make_mosi_frame(CMD_PREVENT_SLEEP, /* data = */ NULL, /* data_len = */ 0, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHT("invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool send_cmd_06(void) {
    /* Frame format:
     *  AA 99 : header
     *  06    : cmd
     *  00    : len
     *  F9    : checksum
     *  AA AA : footer
     */

    DEBUG_SHT("sending command 06");

    uint8 frame_len;
    uint8 *mosi_frame = make_mosi_frame(0x06, /* data = */ NULL, /* data_len = */ 0, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHT("invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool send_cmd_07(void) {
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

    DEBUG_SHT("sending command 07");

    uint8 frame_len;
    uint8 data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    uint8 *mosi_frame = make_mosi_frame(0x06, data, /* data_len = */ 5, &frame_len);
    uint8 miso_frame[frame_len];

    hspi_transfer(mosi_frame, miso_frame, frame_len);
    free(mosi_frame);

    if (!miso_frame_valid(miso_frame, frame_len)) {
        DEBUG_SHT("invalid SPI slave response");
        return FALSE;
    }

    return TRUE;
}

bool recv_measurements(double *temp, double *hum) {
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

    DEBUG_SHT("receiving measurements");

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
        DEBUG_SHT("invalid slave response");
        return FALSE;
    }

    return TRUE;
}

void recv_dummy(uint8 len) {
    DEBUG_SHT("receiving %d dummy bytes", len);

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


void sht_init_ports(void) {
#ifdef HAS_SHT_TEMPERATURE
    port_register(sht_temperature);
#endif
#ifdef HAS_SHT_HUMIDITY
    port_register(sht_humidity);
#endif
}


#endif  /* HAS_SHT */
