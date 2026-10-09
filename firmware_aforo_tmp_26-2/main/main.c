/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// modulos de freerots
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// drivers
#include <driver/gpio.h>

// logs
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

// wifi
#include <esp_wifi.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <nvs_flash.h>

// biblioteclas propias
#include "esp_wifi_types_generic.h"
#include "freertos/portable.h"
#include "freertos/projdefs.h"
#include "global.h"
#include "modules/DHT11/dht11_lib.h"
#include "modules/MQTT/mqtt_lib.h"
#include "modules/TCP/tcp_lib.h"
#include "modules/WIFI/wifi_lib.h"
#include "portmacro.h"
// macros

//+++++++++++++ colas

//+++++++++++++ grupos de eventos
EventGroupHandle_t s_wifi_event_group;
QueueHandle_t tcp_rx_queue;
EventGroupHandle_t g_tcp_event_group;

//++++++++++++++++++++++++++= estrucutras - enums

// estucuturua para red
TYPE_ESP_WIFI_T esp_wifi = {0};
// estrucutra para parametros de la conexion tcp
TYPE_TCP_CLIENT_T tcp_client = {0};
// enum de operacion CP
TYPE_OP_TYPE_T op_type;
// enum de trama bianrio
ACTION_T action;
TYPE_FRAME_T type_frame;
DATA_FIELD_T data_field_t;

// estrucutra de la uncion que agrupa los datos
TYPE_SEND_INFO_T send_info = {0};

TYPE_FORMAT_REQUEST_T format_request = {0};

// ++++++++++++++++++++++++ variables globales ++++++++++++++++++++=

uint8_t g_wifi_type = eWifiDefault;

static const char *TAG = "MAIN_AFORO: ";

// +++++++++++++++++++++++++++++ handle

TaskHandle_t xRecv_task;
TaskHandle_t xTcp_process_task;

//+++++++++++++++++++++++++ funciones ++++++++++++++++++++++++++++

// funciones para flash, para averiguar si hay credenicales guardadas.
esp_err_t nvs_save_str(const char *key, const char *value);
esp_err_t nvs_load_str(const char *key, char *buf, size_t len);
esp_err_t save_wifi_credentials(void);
esp_err_t load_wifi_credentials(void);
/**
 * @note actualizacion de funcion, para aceptar redes de hogar y de epresa
 ***/
esp_err_t update_setup_cred(char *key, char *anchor, char *pswd_ent,
                            char *identificator);

/*
 * @brief funcion encargada de establecer la conexion TCP entre la ESP32 y la
 * RPI.
 *
 * */
void setup_tcp(void);

// tareas

/**
 * @brief tarea encargada de ser el mediador entre lo que se recibe el esp,
 * parsea los datos, detecta que es lo que se pretende hacer, recopila los datos
 * y manda de nuevo para poder mandar la infromacion.
 *
 *
 */

void tcp_process_task(void *params);

// inicio del programa
void app_main(void) {

  // variables
  bool creds_in_nvs = false;
  esp_err_t ret;
  tcp_rx_queue = NULL;
  g_tcp_event_group = NULL;
  s_wifi_event_group = NULL;

  // grupos de eventos
  // creamos el grupo de eventos para WIFI
  s_wifi_event_group = xEventGroupCreate();
  g_tcp_event_group = xEventGroupCreate();

  // colas
  tcp_rx_queue = xQueueCreate(10, sizeof(TYPE_FORMAT_REQUEST_T *));

  /**
   * iniciamos memoria NVS
   *
   * */

  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // +++++++++++++++++++++ Buscar credenciales NVS +++++++++++++++++++++

  ret = load_wifi_credentials();
  creds_in_nvs = (ret == ESP_OK);

  if (!creds_in_nvs) {
    /*****************************************
     *      no hay credenciales, entonces ahora la ESP no lazan una AP, lo que
     * hara la ESP32 es buscar la IP, por defecto que tiene la rasberry pi zero
     * 2 w, que estra corrinedo en pararelo como AP, en la cual por medio de
     * sockets TCP, vamos a solicitar las credenciales de WIFI,
     *
     *      esto con el objetivo de que nomas se tenga que introducir las
     * credenciales 1 vez, por ejemplo al adquirir por primera vez el
     * dispositvo, despues si se adquiere mas o se actualiza las credenciales
     *  * solo se introducen 1 vez y cada esp va a soictar a la rasberry las
     * credenciales o las nuevas la rasberry tiene un maximo de 8 disposiovos
     * conectados a la vez, lo cual no es un problema porque este solo va a
     * sociitar y esperar 1 vez, que es cunado se solciita, se desconecta y se
     * conecta a la red wifi
     *
     *
     *
     ******************************************/
    ESP_LOGW(TAG, "No se encontraron credenciales WiFi en NVS");
    // esta seccion sera esa parte, la coneeccion y obtencion de datos -> puede
    // que mandemos a llama a una funcion, ya veremos

    // vamos a tratar de conectarnos a la red AP de la RPI.
    // esto no es una red como tal, es el internediario para tener las
    // credenicales del WIFI del local / hogar / compania/ etc..

    // por ahora lo dejaremos asi hardcodeado, pero despues lo tendremos que
    // pasara  una macro, pero debe de estar encrptado.

    ret = update_setup_cred("SelfIQ-Admi", "$elfIQ-4dmi-1", NULL, "SETUP_WIFI");
    ESP_ERROR_CHECK(ret);
    wifi_init_sta();
    ESP_LOGI(TAG, "estableciendo conexion con la red AP de la RPI");

    // ahora neceistamos conectarnos al server TCP que se tiene la RPI. (por
    // ahora podemos debuggear por el log, pero vamos a necesitar algo para
    // inidcar al usuario posibles errores)

    ret = update_tcp_config(RPI_IP_SETUP, RPI_PORT_SETUP);

    ESP_ERROR_CHECK(ret);

    /*
     * Esta función debe esperar hasta que tcp_process_task()
     * haya recibido y validado las credenciales.
     *
     * tcp_process_task() utiliza update_setup_cred(),
     * que actualiza directamente:
     *
     * esp_wifi.ssid
     * esp_wifi.pswd
     * esp_wifi.user_name
     * esp_wifi.type_connected
     */

    // @ NOTE - aun me falta realizar validaciones para mandar para aca atras
    // cunado ya se hayan recibido las credenciales.
    setup_tcp();

    // +++++++++++++ Validar credenciales recibidas +++++++++++++

    if (esp_wifi.ssid == NULL || esp_wifi.pswd == NULL ||
        esp_wifi.ssid[0] == '\0') {

      ESP_LOGE(TAG, "Credenciales WiFi incompletas");
      return;
    }

    if (esp_wifi.type_connected == 1 &&
        (esp_wifi.user_name == NULL || esp_wifi.user_name[0] == '\0' ||
         esp_wifi.pswd[0] == '\0')) {

      ESP_LOGE(TAG, "Credenciales Enterprise incompletas");
      return;
    }

    // +++++++++++++ Finalizar comunicación TCP +++++++++++++

    /*
     * Ya recibimos las credenciales.
     * Cerramos las tareas temporales del socket.
     */

    if (xRecv_task != NULL) {
      vTaskDelete(xRecv_task);
      xRecv_task = NULL;
    }

    if (xTcp_process_task != NULL) {
      vTaskDelete(xTcp_process_task);
      xTcp_process_task = NULL;
    }

    if (tcp_client.sock >= 0) {
      close(tcp_client.sock);
      tcp_client.sock = -1;
    }

    tcp_client.connected = 0;

    // +++++++++++++ Conectarse a la red definitiva +++++++++++++

    ESP_LOGI(TAG, "Cambiando a la red WiFi definitiva");

    /*
     * Las credenciales ya están almacenadas en esp_wifi.
     * No necesitamos llamar nuevamente a update_setup_cred().
     */

    wifi_reconnect();

    if (!esp_wifi.connected) {
      ESP_LOGE(TAG, "No se pudo conectar a la red definitiva");
      return;
    }
    // +++++++++++++ Guardar credenciales en NVS +++++++++++++

    ret = save_wifi_credentials();

    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error al guardar credenciales: %s", esp_err_to_name(ret));
      return;
    }

    ESP_LOGI(TAG, "Credenciales almacenadas correctamente en NVS");

  } else {
    // +++++++++++++++++++++ Credenciales existentes +++++++++++++++++++++

    ESP_LOGI(TAG, "Credenciales encontradas en FLASH");

    /*
     * load_wifi_credentials() ya cargó las credenciales
     * directamente en la estructura esp_wifi.
     */

    if (esp_wifi.type_connected == 1) {

      ESP_LOGI(TAG, "Cargando WiFi empresarial");

    } else {

      ESP_LOGI(TAG, "Cargando WiFi normal");
    }

    // Conectarse usando las credenciales almacenadas
    wifi_init_sta();
  }

  // +++++++++++++++++++++ Finalización +++++++++++++++++++++

  if (esp_wifi.connected) {

    ESP_LOGI(TAG, "ESP CONECTADA A LA RED CON EXITO");

  } else {

    ESP_LOGE(TAG, "No se pudo establecer la conexion WiFi");
  }
}

esp_err_t save_wifi_credentials(void) {

  // Validar las credenciales actuales
  if (esp_wifi.ssid == NULL || esp_wifi.pswd == NULL ||
      esp_wifi.ssid[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  bool enterprise = (esp_wifi.type_connected == 1);

  if (enterprise &&
      (esp_wifi.user_name == NULL || esp_wifi.user_name[0] == '\0' ||
       esp_wifi.pswd[0] == '\0')) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;

  esp_err_t ret = nvs_open("storage", NVS_READWRITE, &handle);

  if (ret != ESP_OK)
    return ret;

  // Guardar SSID
  ret = nvs_set_str(handle, "wifi_ssid", esp_wifi.ssid);
  if (ret != ESP_OK)
    goto cleanup;

  // Guardar contraseña
  ret = nvs_set_str(handle, "wifi_pass", esp_wifi.pswd);
  if (ret != ESP_OK)
    goto cleanup;

  // Guardar usuario si es Enterprise
  if (enterprise) {

    ret = nvs_set_str(handle, "wifi_user", esp_wifi.user_name);

  } else {

    // Si antes era Enterprise, borrar el usuario anterior
    ret = nvs_erase_key(handle, "wifi_user");

    if (ret == ESP_ERR_NVS_NOT_FOUND)
      ret = ESP_OK;
  }

  if (ret != ESP_OK)
    goto cleanup;

  // Mantener el formato anterior de wifi_type: 1 o 2
  char type_str[4];

  snprintf(type_str, sizeof(type_str), "%u",
           enterprise ? eWifiEnterprise : eWifiDefault);

  ret = nvs_set_str(handle, "wifi_type", type_str);

  if (ret == ESP_OK)
    ret = nvs_commit(handle);

cleanup:
  nvs_close(handle);
  return ret;
}

esp_err_t load_wifi_credentials(void) {

  char ssid[33] = {0};
  char pass[65] = {0};
  char user[32] = {0};
  char type_str[4] = {0};

  esp_err_t ret;

  ret = nvs_load_str("wifi_ssid", ssid, sizeof(ssid));
  if (ret != ESP_OK)
    return ret;

  ret = nvs_load_str("wifi_pass", pass, sizeof(pass));
  if (ret != ESP_OK)
    return ret;

  ret = nvs_load_str("wifi_type", type_str, sizeof(type_str));
  if (ret != ESP_OK)
    return ret;

  // Evita aceptar valores desconocidos como WiFi normal
  if (ssid[0] == '\0')
    return ESP_ERR_INVALID_ARG;

  if (strcmp(type_str, "1") == 0) {

    return update_setup_cred(ssid, pass, NULL, "SETUP_WIFI");

  } else if (strcmp(type_str, "2") == 0) {

    ret = nvs_load_str("wifi_user", user, sizeof(user));

    if (ret != ESP_OK)
      return ret;

    if (user[0] == '\0' || pass[0] == '\0')
      return ESP_ERR_INVALID_ARG;

    return update_setup_cred(ssid, pass, user, "SETUP_ENTERPRISE_WIFI");
  }

  return ESP_ERR_INVALID_ARG;
}

esp_err_t nvs_load_str(const char *key, char *buf, size_t len) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
  if (err != ESP_OK)
    return err;
  size_t required = len;
  err = nvs_get_str(handle, key, buf, &required);
  nvs_close(handle);
  return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// update_setup_cred
// en caso de WIDI key = ssid y anchor = psw
// para WIFI empresa igual solo que user, es el usuario en la rede de la empresa
//
// -----TCP
// para socket TCP, ket = IP_SERVER , anchor = PORT_IP
// ─────────────────────────────────────────────────────────────────────────────
esp_err_t update_setup_cred(char *key, char *anchor, char *user, char *type) {

  if (strcmp(type, "SETUP_WIFI") == 0) {
    esp_wifi.ssid = realloc(esp_wifi.ssid, strlen(key) + 1);
    esp_wifi.pswd = realloc(esp_wifi.pswd, strlen(anchor) + 1);
    if (esp_wifi.ssid != NULL && esp_wifi.pswd != NULL) {
      strcpy(esp_wifi.ssid, key);
      strcpy(esp_wifi.pswd, anchor);
      free(esp_wifi.user_name);
      esp_wifi.user_name = NULL;
      esp_wifi.type_connected = 0;
      return ESP_OK;
    }
    ESP_LOGE(TAG, "no se pudo asignar memoria");
    return ESP_FAIL;
  }

  else if (strcmp(type, "SETUP_ENTERPRISE_WIFI") == 0) {
    esp_wifi.ssid = realloc(esp_wifi.ssid, strlen(key) + 1);
    esp_wifi.pswd = realloc(esp_wifi.pswd, strlen(anchor) + 1);
    esp_wifi.user_name = realloc(esp_wifi.user_name, strlen(user) + 1);
    if (esp_wifi.ssid != NULL && esp_wifi.pswd != NULL &&
        esp_wifi.user_name != NULL) {
      strcpy(esp_wifi.ssid, key);
      strcpy(esp_wifi.pswd, anchor);
      strcpy(esp_wifi.user_name, user);
      esp_wifi.type_connected = 1;
      return ESP_OK;
    }

    return ESP_FAIL;
  }

  return ESP_FAIL;
}

void tcp_process_task(void *params) {

  TYPE_FORMAT_REQUEST_T *frame;

  // uint8_t buffer[MAX_DATA]; // para mostrar mensajes por UART
  esp_err_t ret;

  // uint8_t frame_len;
  //  int len;
  int offset = 0;
  while (1) {

    // recibira los datos e por cola
    if (xQueueReceive(tcp_rx_queue, &frame, portMAX_DELAY)) {

      // ACK: identificador ACK y len != 0xFF
      if (frame->header == ACK && frame->len != 0xFF) {

        ESP_LOGI(TAG, "ACK recibido");

        uint8_t data_len = frame->len - 1;
        uint8_t *ssid_aux = NULL;
        uint8_t *pswd_aux = NULL;
        uint8_t *user_aux = NULL;

        /*uint8_t ssid_len = 0;
        uint8_t pswd_len = 0;
        uint8_t user_len = 0;*/

        uint8_t enterprise_frame =
            0; // 0 indica que no de empresa, si se activa en 1, indica que si
               // es de empresa la red

        // entonces cuando viene con ACK de parte de la RPI, trae las
        // credenciales pero aun no estan fromateadas

        // indicando que sera una red normal.
        if (frame->action == eRespWifi) {
          // wifi de casa, o privada que solo requiere el SSID y PSWD

          // creamos una copia de data en buffer para manerajr de mejor manera
          // este punto
          while (offset < data_len) {

            // verificamos que venga la estrucutra basica
            if ((offset + 2) > data_len) {
              // @ERROR frame con informacion incompleta
              ESP_LOGI(TAG, "seccion de DATA incompleta");
              break;
            }

            uint8_t field_id = frame->data[offset++]; // ID del inicio
            uint8_t field_len =
                frame->data[offset++]; // tamnio de la infromacion (tamanio en
                                       // bytes de SSID)

            if (field_id != eDataSSID && field_id != eDataPSWD &&
                field_id != eDataUSR) {
              // entonces el frame tiene un error por lo que salidmos e
              // indicamos squi el error
              //@ERROR frame con inicio incorrecto, desechamaos todo el frame
              ESP_LOGE(TAG, "\r\nFrame con ID incorrecto\r\n");
              break;
            }

            if (field_id == eDataUSR && frame->type_frame != eWifiEnterprise) {

              ESP_LOGE(TAG, "Campo USER recibido en red no Enterprise");
              break;
            }

            uint8_t bytes_remaining = data_len - offset;

            if (field_len > bytes_remaining) {
              ESP_LOGE(TAG,
                       "Campo incompleto para red disponibles, espacio "
                       "necesario: %u, se teine %u",
                       field_len, bytes_remaining);
              break;
            }

            if (field_id == eDataSSID) {
              // evitando que se reciba 2 veces el SSID
              if (ssid_aux != NULL) {
                ESP_LOGE(TAG, "SSID duplicado");
                break;
              }

              /*
               * 32 bytes MAX de len de SSID + '\0'
               *
               * */

              if (field_len > SSID_LEN_MAX) {
                ESP_LOGE(TAG, "SSID demsaiado largo: %u", field_len);
                break;
              }

              // asignamos memoria a SSID
              ssid_aux = malloc(field_len + 1);

              if (ssid_aux == NULL) {
                ESP_LOGE(TAG, "no se pudo asingar memoeria para SSID");
                break;
              }

              memcpy(ssid_aux, &frame->data[offset], field_len);
              ssid_aux[field_len] = '\0';
              // ssid_len = field_len;
            } else if (field_id == eDataPSWD) {
              if (pswd_aux != NULL) {
                ESP_LOGE(TAG, "PSWD duplicado");
                break;
              }

              if (field_len > PSWD_LEN_MAX) {
                ESP_LOGE(TAG, "password demsaiado largo: %u", field_len);
                break;
              }

              // el PSWD esta muy bien
              pswd_aux = malloc(field_len + 1);
              if (pswd_aux == NULL) {
                ESP_LOGE(TAG, "no se pudo asingar memoeria para password");
                break;
              }

              memcpy(pswd_aux, &frame->data[offset], field_len);
              pswd_aux[field_len] = '\0';
              // pswd_len = field_len;
            } else if (field_id == eDataUSR &&
                       frame->type_frame == eWifiEnterprise) {

              if (user_aux != NULL) {
                ESP_LOGE(TAG, "USER duplicado");
                break;
              }

              if (field_len == 0 || field_len > USER_LEN_MAX) {
                ESP_LOGE(TAG, "Longitud USER invalida: %u", field_len);
                break;
              }

              user_aux = malloc(field_len + 1);

              if (user_aux == NULL) {
                ESP_LOGE(TAG, "No se pudo asignar memoria para USER");
                break;
              }

              memcpy(user_aux, &frame->data[offset], field_len);
              user_aux[field_len] = '\0';

              // user_len = field_len;
              enterprise_frame = 1;
            }

            offset += field_len; // avanzamos al sigueinte campo
          }

          if (enterprise_frame == 1) {
            ret = update_setup_cred((char *)ssid_aux, (char *)pswd_aux,
                                    (char *)user_aux, "SETUP_ENTERPRISE_WIFI");
          } else {
            ret = update_setup_cred((char *)ssid_aux, (char *)pswd_aux, NULL,
                                    "SETUP_WIFI");
          }
        }
        free(ssid_aux);
        free(pswd_aux);
        free(user_aux);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);

        // entonces en este momento ya se tienen las credenicales actualizadas.
      }
    }

    // NACK: identificador 0x3501 y len == 0xFF
    else if (frame->header == ACK && frame->len == 0xFF) {
      ESP_LOGE(TAG, "\r\nEl servidor no contesta\r\n");

    }
    // el frame que se envio no concuerda con las estrucutras aceptadas que el
    // programa puede procesar
    else {
      ESP_LOGE(TAG, "\r\nFORMTAMO INCORRECTO\r\n");

      // debemos de enviar un NACK
      send_info.op_type = OP_NACK;
      ret = send_message();
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "\r\nERRO AL SER EL ENVIO DEL FRAME\r\n");
      }
    }
    vPortFree(frame);
    frame = NULL;
  }
}

void setup_tcp(void) {

  // no es tarea, pero realizara procesos que pueden tardar o llevar mas o menos
  // necetiamos una froma de saber que ya solciitamos, las credenicales WIFI.

  static uint8_t req_wifi = 0;
  // esp_err_t ret;
  while (1) {

    esp_err_t ret = tcp_cliente_init();

    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "\r\nconexion con el servidor establecida\r\n");
      xTaskCreate(recv_task, "recv_task", 4098, NULL, 8, &xRecv_task);
      xTaskCreate(tcp_process_task, "tcp_process_task", 4098, NULL, 8,
                  &xTcp_process_task);

      if (req_wifi == 0) {
        // quiere decir que aprnas creamos la tarea talvez o que no hemos
        // solicitado las credenicales para coenctarnos a WIFI.
        send_info.op_type = OPReqCredWifi;
        ret = send_message();
        ESP_ERROR_CHECK(ret);
        req_wifi = 1;
      }

      EventBits_t uxBits = xEventGroupWaitBits(
          s_wifi_event_group, WIFI_CREDS_READY, pdTRUE, pdFALSE, portMAX_DELAY);
      if (uxBits & WIFI_CREDS_READY) {
        req_wifi = 0;
        break;
      }
    }
    // else {
    /*
     * no se pudo crear el socket. con las credeniciales y establecer la
     * conexion.
     * */
    //}
  }
}
