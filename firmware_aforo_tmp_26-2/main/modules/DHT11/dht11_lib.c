#include "dht11_lib.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT11";
static portMUX_TYPE dht11_mux = portMUX_INITIALIZER_UNLOCKED;

int wait_for_state(dht11_t dht11, int state, int timeout) {
  const int64_t start = esp_timer_get_time();
  while (gpio_get_level(dht11.dht11_pin) != state) {
    if (esp_timer_get_time() - start >= timeout)
      return -1;
    ets_delay_us(1);
  }
  return (int)(esp_timer_get_time() - start);
}

void hold_low(dht11_t dht11, int hold_time_us) {
  /* The caller configures open-drain: a high level releases the bus. */
  gpio_set_level(dht11.dht11_pin, 0);
  ets_delay_us(hold_time_us);
  gpio_set_level(dht11.dht11_pin, 1);
}

/* Return the failed phase; no logging or scheduling inside the capture. */
static int capture(dht11_t sensor, uint8_t data[5]) {
  int phase = 0;
  gpio_set_level(sensor.dht11_pin, 0);
  /* At least 18 ms, without keeping interrupts disabled during the start. */
  vTaskDelay(pdMS_TO_TICKS(20) + 1);

  portENTER_CRITICAL(&dht11_mux);
  gpio_set_level(sensor.dht11_pin, 1);
  /* Allow the pull-up to raise DATA before looking for the response. */
  ets_delay_us(10);
  if (wait_for_state(sensor, 0, 100) < 0) {
    phase = 1;
    goto done;
  }
  if (wait_for_state(sensor, 1, 100) < 0) {
    phase = 2;
    goto done;
  }
  if (wait_for_state(sensor, 0, 100) < 0) {
    phase = 3;
    goto done;
  }
  for (int bit = 0; bit < 40; ++bit) {
    if (wait_for_state(sensor, 1, 100) < 0) {
      phase = 4;
      goto done;
    }
    int high_us = wait_for_state(sensor, 0, 100);
    if (high_us < 0) {
      phase = 4;
      goto done;
    }
    /* DHT11 high pulse: 26-28 us for zero, about 70 us for one. */
    data[bit / 8] |= (high_us > 50) << (7 - bit % 8);
  }
done:
  portEXIT_CRITICAL(&dht11_mux);
  return phase;
}

int dht11_read(dht11_t *dht11, int connection_timeout) {
  if (!dht11 || connection_timeout <= 0 ||
      !GPIO_IS_VALID_OUTPUT_GPIO(dht11->dht11_pin))
    return -1;

  gpio_config_t config = {
      .pin_bit_mask = 1ULL << dht11->dht11_pin,
      .mode = GPIO_MODE_INPUT_OUTPUT_OD,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_set_level(dht11->dht11_pin, 1);
  if (gpio_config(&config) != ESP_OK)
    return -1;

  for (int attempt = 0; attempt < connection_timeout; ++attempt) {
    /* Also gives the sensor time to settle before the first request. */
    vTaskDelay(pdMS_TO_TICKS(2000) + 1);
    if (!gpio_get_level(dht11->dht11_pin)) {
      ESP_LOGW(TAG, "GPIO%d DATA bajo en reposo; revisar cableado/pull-up",
               dht11->dht11_pin);
      continue;
    }

    uint8_t data[5] = {0};
    int phase = capture(*dht11, data);
    if (phase) {
      ESP_LOGW(TAG, "GPIO%d intento %d/%d: timeout fase %d (%s)",
               dht11->dht11_pin, attempt + 1, connection_timeout, phase,
               phase == 1 ? "sin respuesta inicial" : "respuesta incompleta");
      continue;
    }
    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
      ESP_LOGW(TAG, "Checksum incorrecto en intento %d/%d",
               attempt + 1, connection_timeout);
      continue;
    }
    dht11->humidity = data[0] + data[1] / 10.0f;
    dht11->temperature = data[2] + data[3] / 10.0f;
    return 0;
  }
  return -1;
}
