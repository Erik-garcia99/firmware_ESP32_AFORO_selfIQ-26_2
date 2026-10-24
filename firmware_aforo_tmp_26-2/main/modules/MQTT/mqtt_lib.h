#ifndef SELFIQ_MQTT_LIB_H
#define SELFIQ_MQTT_LIB_H
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
/* Call start/stop only from the connection owner (main), never MQTT callback. */
esp_err_t mqtt_register_start(const char *broker_ip, uint16_t broker_port);
void mqtt_network_stop(void);
bool mqtt_network_connected(void);
/* ESP_OK: accepted in bounded RAM queue, not yet durable. Caller handles errors.
 * Used by each sensor module, e.g. mqtt_queue_value("temperature", 23.5).
 * No UTC clock is invented: gateway attaches received_at. */
esp_err_t mqtt_queue_value(const char *event_type, double value);
#endif
