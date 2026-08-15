#include "sht.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char *TAG = "sht";

#define I2C_FREQ_HZ_DEFAULT  100000
#define I2C_TIMEOUT_MS       20
#define SHT_ADDR_DEFAULT     0x44
#define SHT_CMD_MEAS_HIGH    0xFD

/* Datasheet: 8.3 ms worst case for the high-repeatability measurement.
 * Busy-waiting beats vTaskDelay() here because CONFIG_FREERTOS_HZ is often
 * 100, so a one-tick delay can return after anything up to 10 ms. */
#define SHT_MEAS_US          10000

static i2c_master_dev_handle_t s_dev;

/* Sensirion CRC-8: polynomial 0x31, initial value 0xFF, no final xor. */
static uint8_t sensirion_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t sht_init(const sht_config_t *cfg)
{
    if (cfg == NULL || cfg->bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint16_t addr = cfg->addr == 0 ? SHT_ADDR_DEFAULT : cfg->addr;
    const uint32_t scl_hz = cfg->scl_speed_hz == 0 ? I2C_FREQ_HZ_DEFAULT
                                                   : cfg->scl_speed_hz;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = scl_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(cfg->bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "device: %s", esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    return ESP_OK;
}

esp_err_t sht_read(sht_sample_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t t0 = esp_timer_get_time();

    const uint8_t cmd = SHT_CMD_MEAS_HIGH;
    esp_err_t err = i2c_master_transmit(s_dev, &cmd, sizeof(cmd), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "measure command: %s", esp_err_to_name(err));
        return err;
    }
    esp_rom_delay_us(SHT_MEAS_US);

    uint8_t rx[6];
    err = i2c_master_receive(s_dev, rx, sizeof(rx), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read: %s", esp_err_to_name(err));
        return err;
    }

    if (sensirion_crc8(&rx[0], 2) != rx[2] || sensirion_crc8(&rx[3], 2) != rx[5]) {
        ESP_LOGE(TAG, "crc mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    const uint32_t raw_t = ((uint32_t)rx[0] << 8) | rx[1];
    const uint32_t raw_rh = ((uint32_t)rx[3] << 8) | rx[4];

    /* Datasheet conversions scaled to integers: the result is reported in
     * tenths of a degree and whole percent anyway, so float buys nothing. */
    const int32_t temp_x10 = -450 + (int32_t)((1750U * raw_t + 32767U) / 65535U);
    int32_t humidity = -6 + (int32_t)((125U * raw_rh + 32767U) / 65535U);
    if (humidity < 0) {
        humidity = 0;
    } else if (humidity > 100) {
        humidity = 100;
    }

    const int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;
    out->temp_x10 = (int16_t)temp_x10;
    out->rh = (uint8_t)humidity;
    out->duration_ms = elapsed_ms > UINT8_MAX ? UINT8_MAX : (uint8_t)elapsed_ms;
    return ESP_OK;
}
