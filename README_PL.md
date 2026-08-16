[English](README.md)

# esp32-sensor-sht4x

Komponent temperatury i wilgotności SHT4x (ESP-IDF, czyste C). Aplikacja jest właścicielem magistrali I2C; ten moduł tylko podpina czujnik i wykonuje jeden pomiar wysokiej precyzji.

## Jaki problem rozwiązuje

Urządzenie na baterii, które się budzi, czyta czujnik i wraca do snu, nie powinno chować pinów płytki, pull-upów ani własności I2C w sterowniku. Kolejny układ na tym samym STEMMA QT / I2C nie mógłby wtedy współdzielić magistrali.

Ten moduł bierze bus, który caller już utworzył, dodaje jedno urządzenie SHT4x i zwraca próbkę w liczbach całkowitych. Piny, port i pull-upy zostają w aplikacji. Adres i prędkość SCL są argumentami, więc ten sam kontrakt działa na innej płytce bez edycji komponentu.

## Co moduł robi za Ciebie

- **Jeden pomiar high-repeatability** (`0xFD`), sprawdzony CRC-8, przeliczony na dziesiąte stopnia i pełne procenty RH bez zmiennoprzecinkowych.
- **Bus po stronie callera.** `sht_init()` nigdy nie woła `i2c_new_master_bus` ani `i2c_del_master_bus`. Drugie urządzenie na tej samej magistrali nie wymaga breaking change API.
- **Pokrętła płytki przy init.** Adres `0` znaczy `0x44` (pin ADDR niski); `0x45` to wariant z ADDR wysokim. `scl_speed_hz` `0` znaczy 100 kHz.

## Szybki start

Aplikacja tworzy bus, potem moduł to dwa wywołania:

```c
#include "driver/i2c_master.h"
#include "sht.h"

static i2c_master_bus_handle_t s_bus;

void app_main(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = GPIO_NUM_23,   /* Twoja płytka */
        .scl_io_num = GPIO_NUM_24,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,  /* STEMMA QT ma zewnętrzne pull-upy */
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        return;   /* albo deep sleep — Twoja decyzja */
    }

    /* addr i scl_speed_hz zostają 0 → 0x44 @ 100 kHz */
    if (sht_init(&(sht_config_t){ .bus = s_bus }) != ESP_OK) {
        return;
    }

    sht_sample_t sample;
    if (sht_read(&sample) == ESP_OK) {
        /* sample.temp_x10, sample.rh, sample.duration_ms */
    }
}
```

Uwagi:

- Wołaj `sht_init()` tylko gdy bus już istnieje. Nieudany `i2c_new_master_bus` nie może przekazać pustego handle.
- Nie owijaj wywołań w `ESP_ERROR_CHECK`; każda funkcja zwraca `esp_err_t` i nigdy nie przerywa procesu. Czy nieudany odczyt oznacza panikę, czy „spróbuj przy następnym wybudzeniu” — to Twoja decyzja.
- Po deep sleep proces startuje od nowa: w `app_main` znowu utwórz bus i wołaj `sht_init()`.

## API

`sht_init(const sht_config_t *cfg)` podpina jedno SHT4x do `cfg->bus`.

| Wynik | Kiedy |
| --- | --- |
| `ESP_OK` | urządzenie dodane |
| `ESP_ERR_INVALID_ARG` | `cfg` albo `cfg->bus` jest NULL |
| `ESP_ERR_INVALID_STATE` | już zainicjalizowany |
| błąd sterownika | `i2c_master_bus_add_device` nieudane; moduł zostaje niezainicjalizowany, bus nietknięty |

`sht_read(sht_sample_t *out)` wykonuje jeden pomiar `0xFD`.

| Pole | Znaczenie |
| --- | --- |
| `temp_x10` | °C × 10, np. 235 to 23,5 °C |
| `rh` | wilgotność względna, 0..100 % |
| `duration_ms` | komenda + wait + odczyt; nasycone na 255 |

`*out` jest zerowane przy każdym błędzie. `out == NULL` to `ESP_ERR_INVALID_ARG`. Odczyt przed udanym `sht_init()` to `ESP_ERR_INVALID_STATE`. Zły CRC to `ESP_ERR_INVALID_CRC`.

## Dodanie do projektu

Przy lokalnym checkoutcie obok Twojego projektu (`path` jest względny wobec `main/`):

```yaml
# main/idf_component.yml
dependencies:
  idf: ">=6.0"
  esp32-sensor-sht4x:
    path: ../../esp32-sensor-sht4x
```

Po publikacji repo ta sama zależność może użyć `git:` i tagu wersji zamiast `path:`.

Potem `#include "sht.h"` i budujesz jak zwykle — menedżer komponentów podłącza wszystko podczas `idf.py build`.

Żadne opcje `sdkconfig` nie są wymagane. `sht.h` includuje `driver/i2c_master.h`, bo publiczna konfiguracja niesie handle busa, więc komponent ma `REQUIRES esp_driver_i2c`.

## Warto wiedzieć / ograniczenia

- **Tylko SHT4x.** Komenda `0xFD`. SHT3x to inny protokół i inny moduł.
- **Init tylko podpina urządzenie.** `sht_init()` nie gada z chipem. Obecność, NACK i CRC sprawdza `sht_read()`.
- **Jedna instancja.** Moduł nie jest reentrantny i nie ma mutexa; wołaj go z jednego zadania.
- **Aplikacja jest właścicielem busa.** Piny, port i pull-upy nie są sprawą tego modułu. Nie wrzucaj ich z powrotem do `sht.c`.
- **Busy-wait, nie opóźnienie FreeRTOS.** Konwersja high-repeatability potrzebuje ~8,3 ms; przy `CONFIG_FREERTOS_HZ=100` opóźnienie o jeden tick może przeciągnąć się do 10 ms, więc wait to `esp_rom_delay_us(10000)`.
- **Timeout I2C** transmit/receive to 20 ms i nie jest konfigurowalny.

## Wymagania i licencja

- ESP-IDF **6.0 lub nowszy**; zmierzone na ESP32-C5 (Seeed XIAO, IDF v6.0.2). `idf_component.yml` wymienia tylko `esp32c5` — to jedyny wspierany target, dopóki inna płytka nie zostanie zmierzona i dopisana.
- Licencja: [MIT](LICENSE).
