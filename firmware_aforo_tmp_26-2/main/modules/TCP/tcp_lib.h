/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

#ifndef TCP_LIB_H
#define TCP_LIB_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

//+++++++++++++++++++++++++++ macros
// +++ default IP - rasberry pi

#define RPI_IP_SETUP "192.158.50.1"
#define RPI_PORT_SETUP "5000"

//+++ bit de grupo de eventos

#define RETRY_SERVER BIT7
#define TCP_DISCONNECTED BIT8 // servidor cerro la conexion inesperadamente
#define UPDATE_TCP BIT9
#define NO_RETRY_TCP BIT11

// +++++++++++++++++++ funciones

/**
 * @brief funcion encarga de crear el socket con el que se va a conectar al
 * servidor. lo intentara 5 veces antes de salir e indicar que no se pudo
 * conectar
 *
 * @return ESP_OK cunado se pude establecer la conexion
 * @return ESP_FAIL cunado no se pudo establecer la conexion
 *
 *
 */
esp_err_t tcp_cliente_init();

/**
 * @brief funcion que funcionara para enivar los datos hacia el servidor
 *
 * @param sockfd parametro en donde vendra el descriptro con el sokcer abierto
 * en el momento para enviar la infocmacion
 * @param msg dentro de esta estrucutra se encontrara la informacion del datos a
 * enviar, en especial <op> indicando que estrucutra es la que se va a enviar
 *
 *
 * @return ESP_OK si se envio correctamente
 * @return ESP_FAIL ocurrio un error en el envio de los datos.
 *
 *
 */
esp_err_t send_message();

//++++++++++++++++++++ tareas

void tcp_process_task(void *params);

/**
 * @brief tarea encargada de recibir la peticion/respuesta desde la RPI
 */
void recv_task(void *params);

//++++++++++++++++++++ estucutras

typedef struct {
  char *host_ip;
  uint16_t host_port;
  int sock;
  int connected;
  int logged_in;
} TYPE_TCP_CLIENT_T;

extern TYPE_TCP_CLIENT_T tcp_client;

// ++++++++++++++++++++++++ grupo de eventos

extern EventGroupHandle_t g_tcp_event_group;

#endif
