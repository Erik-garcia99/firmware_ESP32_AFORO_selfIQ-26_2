

#ifndef WIFI_LIB_H
#define WIFI_LIB_H

#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_err.h>

// bits de grupo de eventos

// WIFI
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT2
#define WIFI_UPDATE BIT3
#define BREAK_UPDATE_WIFI BIT4
#define WIFI_CREDS_READY BIT5
#define WIFI_CREDS_RECEIVED BIT6

// definiemos perfiles por defecto a los cuales podemos conectarnos a la red.

extern EventGroupHandle_t s_wifi_event_group;

typedef struct {
  char *ssid;
  char *pswd;
  char *user_name;
  esp_ip4_addr_t *ip;
  int type_connected;
  int connected;
} TYPE_ESP_WIFI_T;

extern TYPE_ESP_WIFI_T esp_wifi;

// prototipos
void wifi_init_sta(void);

/**
 * @brief estblecera las credeniclaes de la red, en el cao que se requiera
 * realizar de nuevo
 *
 * @param SSID un apuntador al nombre de la red
 * @param PSDW un apuntador a la contrasenia de la red
 *
 * @return ESP_OK indica que se pudo realizar el cambio de credenciales
 * @return ESP_FAIL no se pudo estabelcer la credenciales
 *
 * @details lo que hara la funcion es, este va a recibir 2 datos, por lo que
 * estos datos seran copiados en las varibales globales de SSID y PSWD que son
 * los que se estaran usando para conectarse a la red.
 *
 *
 */

/**
 * @brief desconecta y vuelve a conectar con las credenciales actuales en
 * esp_wifi. llamar despues de actualizar esp_wifi.ssid / pswd.
 */
void wifi_reconnect(void);

void wifi_init_ap(void);
void wifi_stop_ap(void);

#endif
