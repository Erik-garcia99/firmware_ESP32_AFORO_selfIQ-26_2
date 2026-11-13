#ifndef DHT11_LIB_H
#define DHT11_LIB_H

#include "esp_log.h"
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include <stdio.h>
#include <string.h>


#define DHT11_DATA_GPIO GPIO_NUM_10
#define DHT11_READ_ATTEMPTS 3
#define DHT11_TASK_STACK_BYTES 4096U
#define DHT11_TASK_PRIORITY 4U
#define DHT11_SAMPLE_INTERVAL_MS 15000U
#define DHT11_EVENT_TEMPERATURE "temperature"
#define DHT11_EVENT_HUMIDITY "humidity"



/**
 * Structure containing readings and info about the dht11
 * @var dht11_pin the pin associated with the dht11
 * @var temperature last temperature reading
 * @var humidity last humidity reading
 */
typedef struct {
  int dht11_pin;
  float temperature;
  float humidity;
} dht11_t;
/**
 * @brief Wait on an already configured input pin until it reaches the state
 * @return returns either the time waited or -1 in the case of a timeout
 * @param state state to wait for
 * @param timeout if counter reaches timeout the function returns -1
 */
int wait_for_state(dht11_t dht11, int state, int timeout);
/**
 * @brief Holds an already configured open-drain pin low, then releases it
 * @param hold_time_us time to hold the pin low for in microseconds
 */
void hold_low(dht11_t dht11, int hold_time_us);
/**
 * @brief The function for reading temperature and humidity values from the
 * dht11
 * @note Call from task context, with a single caller per sensor. Each attempt
 * waits at least 2 seconds using vTaskDelay. Only the response capture (about
 * 4 ms) runs in a critical section. An external DATA pull-up is recommended.
 * @param connection_timeout the number of connection attempts before declaring
 * a timeout
 */
int dht11_read(dht11_t *dht11, int connection_timeout);

#endif
