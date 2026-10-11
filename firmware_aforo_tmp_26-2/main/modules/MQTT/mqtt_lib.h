/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

#ifndef MQTT_LIB_H
#define MQTT_LIB_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

//bits que nos sirven de guia sobre el proceso en donde estamos. 

extern EventGroupHandle_t g_mqtt_event_group;


/**
 * @brief iniclaicion del cliente MQTT y la conecion al broker, asi como suscripcion y publicaciones inicales indicando la conexion. y el setup
 *
 * @param event_group  Event group used to wait for WiFi connection.
 * @param broker_ip    IP address of the MQTT broker (e.g., "192.168.1.100").
 * @return ESP_OK on success, ESP_FAIL otherwise.
 */
esp_err_t mqtt_init(EventGroupHandle_t event_group, const char *broker_ip);



/**
 * @brief Tarea que envía un heartbeat simple cada 30 segundos.
 *
 * @param arg  No usado (NULL).
 *
 * Publica un mensaje en `rackiq/shelf/<id>/heartbeat` con contenido "alive".
 */
void task_mqtt_heartbeat(void *arg);





#endif