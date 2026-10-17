/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

// liberias estanadar
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// logs
#include "esp_err.h"
#include "esp_log.h"
// flash
#include "nvs.h"
#include "nvs_flash.h"
// wifi (para leer lo que el stack guardo en flash)
#include "esp_wifi.h"

// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// librerias propias
#include "global.h"
#include "modules/TCP/tcp_lib.h"
#include "modules/WIFI/wifi_lib.h"

//+++++++++++++++++++++++++++++++++++ colas

QueueHandle_t
    tcp_rx_queue; // cola que usaremos como swap entre el modulo de TCP que
                  // recibe el frame y main que desempaqueta la infromacion.

//++++++++++++++++++++++++++++++++++ grupo de eventos
EventGroupHandle_t g_tcp_event_group;

//++++++++++++++++++++++++++++++++ macros

// namespace propio en NVS donde guardamos nuestros datos y sus checksums
#define NVS_NS "selfiq_cfg"
#define KEY_WIFI_SUM "wifi_sum" // checksum de SSID + pswd
#define KEY_TCP_IP "tcp_ip"
#define KEY_TCP_PORT "tcp_port"
#define KEY_TCP_SUM "tcp_sum" // checksum de IP + PORT

//++++++++++++++++++++++++++++++++ varibales globales

const char *TAG = "MAIN: ";

//++++++++++++++++++++++++++++++++ estrucutras
TYPE_FORMAT_REQUEST_T format_reques = {0};
TYPE_SEND_INFO_T send_info = {0};
TYPE_ESP_WIFI_T esp_wifi = {0};
TYPE_TCP_CLIENT_T tcp_client = {0};

//++++++++++++++++++++++++++++++++++ enums

TYPE_OP_TYPE_T eOp_type;
ACTION_T eAction;
TYPE_FRAME_T eType_frame;
DATA_FIELD_T eData_field_t;

//+++++++++++++++++++++++++++++++funciones

// static uint8_t request_credentials(char **ssid, char **pswd);

// funciones que usare para guardar crecnuales en flash, (WIFI lo hace cons us
// propias funciones)
esp_err_t save_cred_tcp(void);
esp_err_t load_cred_tcp(void);

/*
 * @brief funcion que se encarga de actualiuzar las credencuales.
 *
 * */
esp_err_t update_setup_cred(char *key, void *anchor, uint8_t identificator);

/*
 * @brief suma el valor de todos los bytes de los datos.
 * WIFI: key = SSID, anchor = pswd (string)
 * TCP:  key = IP,   anchor = puerto (uint16_t *)
 * */
uint64_t checksum(const char *key, const void *anchor, uint8_t identificator);

// guarda en flash el checksum de las credenciales WIFI
static esp_err_t save_wifi_sum(uint64_t sum);
// lee de flash lo que guardo el stack WIFI, recalcula la suma y la compara con
// la guardada. TRUE si existen y coinciden
static uint8_t wifi_cred_valid(void);

//++++++++++++++++++++++++++++++++ tareas

void app_main(void) {

  // variables
  esp_err_t ret;
  EventBits_t bits = 0;
  // inicalizacion de varibales
  tcp_rx_queue = xQueueCreate(10, sizeof(TYPE_FORMAT_REQUEST_T *));

  // incializacion de flash para cargar o escribir en flash
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // credenciales TCP guardadas (se validan con su checksum)
  ret = load_cred_tcp();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "credenciales TCP no cargadas: %s", esp_err_to_name(ret));
  }

  // WIFI: primero intenta con las credenciales guardadas en flash
  wifi_init_sta();

  if (wifi_cred_valid()) {
    wifi_connect_saved();
  } else {
    ESP_LOGW(TAG, "credenciales WIFI inexistentes o checksum invalido");
    xEventGroupSetBits(g_wifi_event_group, WIFI_NO_CREDS_BIT);
  }

  while (1) {
    // primero se intenta conectar con las credenclaes que puede estar guardasd
    // en flash
    bits = xEventGroupWaitBits(g_wifi_event_group,
                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT |
                                   WIFI_NO_CREDS_BIT,
                               pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
      ESP_LOGI(TAG, "WIFI conectado");
      break;
      // si me pude conectar al WIFI el proximo paso es conectarme a TCP
    } else if (bits & WIFI_NO_CREDS_BIT) {

      ESP_LOGW(TAG,
               "imposble conectarse, no hay crenciales o fallo al intentar");
      // tenemos 2 opciones
      /* realmente la esp32 viene vacia y no tiene crencuales guardadas --> por
       * lo que lo mas porabale es que la red ed la rasberry este levantada al
       * igual que le TCP_server, por lo que por este no hay problema
       *
       * puede que las credenicales esten bien pero hay una falla en internet,
       *
       * en otro caso tiene credennciales guardadas, en este caso, pueden ser
       * uno de los sigueintes casos :
       *
       * la red esta caida.
       * se cambio las credenciales de red
       * se corrompieron las credncieles
       *
       * --> al final cada ESP con MQTT tendra que enviar un keep_alive,
       * entpnces si no se recibe un keep_alive en un lapso de 5 a 10 minutos se
       * va a concluir que algo paso con la conexion en ese esp32
       *
       * por lo que indicara en la APP se se incie comunicacion, es cunado se
       * levanta TCP, y tiene que inciar una comnucacion entre la rasberry y la
       * esp32.
       *
       * */

      // en este caso

      continue;
    } else if (bits & WIFI_FAIL_BIT) {
    }
  }
}

// el seungdo parametro represetna en WIFI la contrasneia
// en TCP represeta en puerto
// como en uno es string y en otro es numerico, se utiliza de tipoc void* para
// evitar el problema de casting, accedindo a memoria
esp_err_t update_setup_cred(char *key, void *anchor, uint8_t identificator) {

  if (identificator == eTCP_CRED_update) {
    // entonces debemos de actualizar las credenicales de TPC
    uint16_t *port = (uint16_t *)anchor;

    tcp_client.host_port = *port;
    tcp_client.host_ip = realloc(tcp_client.host_ip, strlen(key) + 1);

    if (tcp_client.host_ip == NULL) {
      ESP_LOGE(TAG, "no se pudo asingar memoria\n");
      return ESP_FAIL;
    }
    strcpy(tcp_client.host_ip, key);

    // guarda IP + PORT y su checksum en flash
    return save_cred_tcp();

  } else if (identificator == eWIFI_CRED_UPDATE) {
    // key = SSID, anchor = pswd. wifi_set_credentials las guarda en flash (lo
    // hace el stack) y vuelve a intentar la conexion
    esp_err_t ret = wifi_set_credentials(key, (const char *)anchor);
    if (ret != ESP_OK) {
      return ret;
    }

    // guardamos la suma de lo que acabamos de mandar a flash
    return save_wifi_sum(checksum(key, anchor, eWIFI_CRED_UPDATE));
  }

  return ESP_ERR_INVALID_ARG;
}

// vamos a sumar los valores, totales de los datos
// SSID + pswd
// IP + PORT
        {
  uint64_t total = 0;

  // key siempre es un string (SSID o IP): sumamos el valor de cada caracter
  for (size_t i = 0; key[i] != '\0'; i++) {
    total += (uint8_t)key[i];
  }

  if (identificator == eTCP_CRED_update) {
    // anchor es el puerto: es un numero, se suma directo
    total += *(const uint16_t *)anchor;

  } else if (identificator == eWIFI_CRED_UPDATE) {
    // anchor es la contrasenia: otro string, se suma caracter por caracter
    const char *pswd = (const char *)anchor;
    for (size_t i = 0; pswd[i] != '\0'; i++) {
      total += (uint8_t)pswd[i];
    }
  }

  return total;
}

//+++++++++++++++++++++++++++++++ flash: TCP

esp_err_t save_cred_tcp(void) {
  if (tcp_client.host_ip == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint16_t port = tcp_client.host_port;
  uint64_t sum = checksum(tcp_client.host_ip, &port, eTCP_CRED_update);

  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_str(h, KEY_TCP_IP, tcp_client.host_ip);
  if (err == ESP_OK) {
    err = nvs_set_u16(h, KEY_TCP_PORT, port);
  }
  if (err == ESP_OK) {
    err = nvs_set_u64(h, KEY_TCP_SUM, sum);
  }
  if (err == ESP_OK) {
    err = nvs_commit(h); // sin commit no se escribe en flash
  }

  nvs_close(h);
  return err;
}

esp_err_t load_cred_tcp(void) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
  if (err != ESP_OK) {
    return err;
  }

  char *ip = NULL;
  size_t len = 0;
  uint16_t port = 0;
  uint64_t saved_sum = 0;

  // con buffer NULL, nvs_get_str solo devuelve el tamanio (incluye el '\0')
  err = nvs_get_str(h, KEY_TCP_IP, NULL, &len);
  if (err == ESP_OK) {
    ip = malloc(len);
    if (ip == NULL) {
      err = ESP_ERR_NO_MEM;
    }
  }
  if (err == ESP_OK) {
    err = nvs_get_str(h, KEY_TCP_IP, ip, &len);
  }
  if (err == ESP_OK) {
    err = nvs_get_u16(h, KEY_TCP_PORT, &port);
  }
  if (err == ESP_OK) {
    err = nvs_get_u64(h, KEY_TCP_SUM, &saved_sum);
  }
  nvs_close(h);

  // recalculamos la suma con lo que leimos y la comparamos con la guardada
  if (err == ESP_OK && checksum(ip, &port, eTCP_CRED_update) != saved_sum) {
    err = ESP_ERR_INVALID_CRC;
  }

  if (err != ESP_OK) {
    free(ip);
    return err;
  }

  free(tcp_client.host_ip);
  tcp_client.host_ip = ip;
  tcp_client.host_port = port;
  return ESP_OK;
}

//+++++++++++++++++++++++++++++++ flash: WIFI (checksum)

static esp_err_t save_wifi_sum(uint64_t sum) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_u64(h, KEY_WIFI_SUM, sum);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }

  nvs_close(h);
  return err;
}

static uint8_t wifi_cred_valid(void) {
  // lo que el stack guardo en flash cuando se llamo esp_wifi_set_config
  wifi_config_t cfg = {0};
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK ||
      cfg.sta.ssid[0] == '\0') {
    return FALSE;
  }

  // ssid (32) y password (64) pueden venir sin '\0' si ocupan todo el
  // buffer, por eso se copian a arreglos con un byte extra en cero
  char ssid[sizeof(cfg.sta.ssid) + 1] = {0};
  char pswd[sizeof(cfg.sta.password) + 1] = {0};
  memcpy(ssid, cfg.sta.ssid, sizeof(cfg.sta.ssid));
  memcpy(pswd, cfg.sta.password, sizeof(cfg.sta.password));

  uint64_t actual = checksum(ssid, pswd, eWIFI_CRED_UPDATE);

  // suma que guardamos nosotros al momento de dar las credenciales
  nvs_handle_t h;
  uint64_t saved = 0;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    return FALSE;
  }
  esp_err_t err = nvs_get_u64(h, KEY_WIFI_SUM, &saved);
  nvs_close(h);

  return (err == ESP_OK && saved == actual) ? TRUE : FALSE;
}

// tareas