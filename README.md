[Polski](README_PL.md)

# esp32-sensor-sht4x

An SHT4x temperature and humidity component (ESP-IDF, plain C). The application owns the I2C bus; this module only attaches the sensor and runs one high-precision measurement.

## The problem it solves

A battery device that wakes, reads a sensor, and goes back to sleep should not bury board pins, pull-ups, or I2C ownership inside the driver. The next sensor on the same STEMMA QT / I2C bus would then be unable to share it.

This module takes a bus the caller already created, adds one SHT4x device, and returns an integer sample. Pins, port, and pull-ups stay in the application. Address and SCL speed are arguments, so the same binary contract works on another board without editing the component.

## What the module handles for you

- **One high-repeatability measurement** (`0xFD`), CRC-8 checked, converted to tenths of a degree and whole-percent RH without floating point.
- **Caller-owned bus.** `sht_init()` never calls `i2c_new_master_bus` or `i2c_del_master_bus`. A second device on the same bus does not need an API break.
- **Board knobs at init.** Address `0` means `0x44` (ADDR pin low); `0x45` is the ADDR-high variant. `scl_speed_hz` `0` means 100 kHz.

## Quick start

The application creates the bus, then the module is two calls:

```c
#include "driver/i2c_master.h"
#include "sht.h"

static i2c_master_bus_handle_t s_bus;

void app_main(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = GPIO_NUM_23,   /* your board */
        .scl_io_num = GPIO_NUM_24,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,  /* STEMMA QT has external pull-ups */
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        return;   /* or deep sleep — your decision */
    }

    /* addr and scl_speed_hz stay 0 → 0x44 @ 100 kHz */
    if (sht_init(&(sht_config_t){ .bus = s_bus }) != ESP_OK) {
        return;
    }

    sht_sample_t sample;
    if (sht_read(&sample) == ESP_OK) {
        /* sample.temp_x10, sample.rh, sample.duration_ms */
    }
}
```

Notes:

- Call `sht_init()` only after the bus exists. A failed `i2c_new_master_bus` must not pass a null handle.
- Don't wrap the calls in `ESP_ERROR_CHECK`; every function returns `esp_err_t` and never aborts. Whether a failed read means panic or "try again next wake" is your decision.
- After deep sleep the process restarts: create the bus and call `sht_init()` again in `app_main`.

## API

`sht_init(const sht_config_t *cfg)` attaches one SHT4x to `cfg->bus`.

| Result | When |
| --- | --- |
| `ESP_OK` | device added |
| `ESP_ERR_INVALID_ARG` | `cfg` or `cfg->bus` is NULL |
| `ESP_ERR_INVALID_STATE` | already initialised |
| driver error | `i2c_master_bus_add_device` failed; module stays uninitialised, bus untouched |

`sht_read(sht_sample_t *out)` runs one `0xFD` measurement.

| Field | Meaning |
| --- | --- |
| `temp_x10` | °C × 10, e.g. 235 is 23.5 °C |
| `rh` | relative humidity, 0..100 % |
| `duration_ms` | command + wait + read; saturated at 255 |

`*out` is zeroed on every failure. `out == NULL` is `ESP_ERR_INVALID_ARG`. A read before a successful `sht_init()` is `ESP_ERR_INVALID_STATE`. A bad CRC is `ESP_ERR_INVALID_CRC`.

## Adding it to a project

For a local checkout sitting next to your project (`path` is relative to `main/`):

```yaml
# main/idf_component.yml
dependencies:
  idf: ">=6.0"
  esp32-sensor-sht4x:
    path: ../../esp32-sensor-sht4x
```

After you publish the repo, the same dependency can use `git:` and a version tag instead of `path:`.

Then `#include "sht.h"` and build as usual — the component manager picks it up during `idf.py build`.

No `sdkconfig` options are required. `sht.h` includes `driver/i2c_master.h` because the public config carries a bus handle, so the component `REQUIRES esp_driver_i2c`.

## Good to know / limits

- **SHT4x only.** Command `0xFD`. SHT3x is a different protocol and a different module.
- **One instance.** The module is not reentrant and has no mutex; call it from a single task.
- **The caller owns the bus.** Pins, port, and pull-ups are not this module's business. Do not move them back into `sht.c`.
- **Busy-wait, not a FreeRTOS delay.** The high-repeatability conversion needs ~8.3 ms; at `CONFIG_FREERTOS_HZ=100` a one-tick delay can overshoot by up to 10 ms, so the wait is `esp_rom_delay_us(10000)`.
- **I2C timeout** for transmit/receive is 20 ms and is not configurable.

## Requirements and license

- ESP-IDF **6.0 or newer**; developed on ESP32-C5 (Seeed XIAO, IDF v6.0.2). Other targets with the I2C master driver should work but are unverified.
- License: [MIT](LICENSE).
