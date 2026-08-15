#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One SHT4x sample. Every field is an integer so a caller can serialise it
 * without parsing a formatted string.
 */
typedef struct {
    int16_t temp_x10;    /* °C × 10, e.g. 235 means 23.5 °C */
    uint8_t rh;          /* relative humidity, 0..100 % */
    uint8_t duration_ms; /* command + wait + read; saturated at 255 */
} sht_sample_t;

/**
 * Board-side I2C setup. The caller owns the bus for its whole lifetime; this
 * module only adds an SHT4x device to it.
 *
 * addr 0 means 0x44 (ADDR pin low). 0x45 is the ADDR-high variant.
 * scl_speed_hz 0 means 100 kHz.
 */
typedef struct {
    i2c_master_bus_handle_t bus;
    uint16_t addr;
    uint32_t scl_speed_hz;
} sht_config_t;

/**
 * Attach one SHT4x to an existing I2C bus. Does not create or delete the bus.
 *
 * Returns ESP_ERR_INVALID_ARG when cfg or cfg->bus is NULL, and
 * ESP_ERR_INVALID_STATE when already initialised. A failed add_device leaves
 * the module uninitialised so a retry is safe; the caller's bus is untouched.
 */
esp_err_t sht_init(const sht_config_t *cfg);

/**
 * Run one high-precision (0xFD) measurement and fill *out.
 *
 * Zeroes *out on every failure so a caller never reads a half-filled or stale
 * sample. Returns ESP_ERR_INVALID_ARG for a NULL out, ESP_ERR_INVALID_STATE
 * when sht_init() has not succeeded, ESP_ERR_INVALID_CRC on a corrupted
 * answer, and the driver error otherwise.
 */
esp_err_t sht_read(sht_sample_t *out);

#ifdef __cplusplus
}
#endif
