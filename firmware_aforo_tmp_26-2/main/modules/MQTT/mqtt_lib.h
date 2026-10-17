#ifndef MQTT_LIB_H
#define MQTT_LIB_H
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "modules/TCP/broker_parser.h"
#define SELFIQ_MQTT_DEFAULT_PORT 1883
#define MQTT_CONNECTED_BIT BIT0
#define MQTT_SUBSCRIBED_BIT BIT1
#define MQTT_REGISTERED_BIT BIT2
extern EventGroupHandle_t g_mqtt_event_group;
/* Solo app_main inicia/detiene/configura. El broker recibido requiere puerto.
 * mqtt_init usa el puerto guardado o 1883 si aun no hay configuracion. */
esp_err_t update_mqtt_config(const char *ip, uint16_t port);
esp_err_t mqtt_load_broker(selfiq_broker_t *broker);
esp_err_t mqtt_save_broker(void);
esp_err_t mqtt_init(EventGroupHandle_t wifi_events, const char *broker_ip);
esp_err_t mqtt_stop(void);
esp_err_t mqtt_get_node_id(char *output, size_t capacity);
/* Publicacion generica de sensores; requiere conexion e ID persistido. */
esp_err_t mqtt_publish(const char *topic, const char *payload, int qos, bool retain);
void task_mqtt_heartbeat(void *arg);
#endif
