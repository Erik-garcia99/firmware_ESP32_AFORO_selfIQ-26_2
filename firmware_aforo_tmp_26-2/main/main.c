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

//++++++++++++++++++++++++++= estrucutras - enums

// estucuturua para red
TYPE_ESP_WIFI_T esp_wifi;
// estrucutra para parametros de la conexion tcp
TYPE_TCP_CLIENT_T tcp_client = {0};
// enum de operacion CP
TYPE_OP_TYPE_T op_type;
// enum de trama bianrio
ACTION_T action;

TYPE_FRAME_T type_frame;

// estrucutra de la uncion que agrupa los datos
TYPE_SEND_INFO_T send_info = {0};

TYPE_FORMAT_REQUEST_T format_request = {0};

// ++++++++++++++++++++++++ variables globales ++++++++++++++++++++=

char g_wifi_ssid[33] = {0};
char g_wifi_pass[65] = {0};
char g_wifi_user[32] = {0};

uint8_t g_wifi_type = eWifiDefault;

const char *TAG = "MAIN_AFORO: ";

// +++++++++++++++++++++++++++++ handle

TaskHandle_t xRecv_task;
TaskHandle_t xTcp_process_task;

//+++++++++++++++++++++++++ funciones ++++++++++++++++++++++++++++

// funciones para flash, para averiguar si hay credenicales guardadas.
esp_err_t nvs_save_str(const char *key, const char *value);
esp_err_t nvs_load_str(const char *key, char *buf, size_t len);
esp_err_t save_wifi_credentials(void);
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

  // creamos el grupo de eventos para WIFI
  s_wifi_event_group = xEventGroupCreate();
  g_tcp_event_group = xEventGroupCreate();

  // grupo de evento para TCP
  tcp_rx_queue = xQueueCreate(10, sizeof(TYPE_FORMAT_REQUEST_T *));
  esp_err_t ret;

  /**
   * lo primero que tendriamos que hacer es inciar WIFI,
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
  memset(g_wifi_ssid, 0, sizeof(g_wifi_ssid));
  memset(g_wifi_pass, 0, sizeof(g_wifi_pass));
  memset(g_wifi_user, 0, sizeof(g_wifi_user));

  char wifi_type_str[4] = {0};

  // --- Carga/obtencion de credenciales WiFi
  bool creds_in_nvs = false;

  esp_err_t ret_ssid =
      nvs_load_str("wifi_ssid", g_wifi_ssid, sizeof(g_wifi_ssid));

  esp_err_t ret_pass =
      nvs_load_str("wifi_pass", g_wifi_pass, sizeof(g_wifi_pass));

  esp_err_t ret_type =
      nvs_load_str("wifi_type", wifi_type_str, sizeof(wifi_type_str));

  if (ret_ssid == ESP_OK && ret_pass == ESP_OK && ret_type == ESP_OK &&
      g_wifi_ssid[0] != '\0') {

    int type = atoi(wifi_type_str);

    if (type == eWifiDefault) {
      creds_in_nvs = true;
    } else if (type == eWifiEnterprise) {

      if (nvs_load_str("wifi_user", g_wifi_user, sizeof(g_wifi_user)) ==
              ESP_OK &&
          g_wifi_user[0] != '\0') {

        creds_in_nvs = true;
      }
    }

    if (creds_in_nvs) {
      g_wifi_type = type;
    }
  }

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

    // esta seccion sera esa parte, la coneeccion y obtencion de datos -> puede
    // que mandemos a llama a una funcion, ya veremos

    // vamos a tratar de conectarnos a la red AP de la RPI.
    // esto no es una red como tal, es el internediario para tener las
    // credenicales del WIFI del local / hogar / compania/ etc..

    // por ahora lo dejaremos asi hardcodeado, pero despues lo tendremos que
    // pasara  una macro, pero debe de estar encrptado.
    update_setup_cred("SelfIQ-Admi", "$elfIQ-4dmi-1", NULL, "SETUP_WIFI");

    // nos conectamos al WIFI
    // xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);
    wifi_init_sta();

    // xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    // ahora neceistamos conectarnos al server TCP que se tiene la RPI. (por
    // ahora podemos debuggear por el log, pero vamos a necesitar algo para
    // inidcar al usuario posibles errores)

    update_setup_cred(RPI_IP_SETUP, RPI_PORT_SETUP, NULL, "SETUP_TCP_CLIENT");
    setup_tcp();

    // regreso con las credenicales ya actualizadas entonces ahora necesito
    // conectarme
    wifi_init_sta();
    // Restaurar el bit que fue borrado en wifi_init_sta()
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    // si ya se conecto entonces no vamos a ocupar las tareas de tcp
    vTaskDelete(xRecv_task);
    vTaskDelete(xTcp_process_task);

  } else {
    ESP_LOGI(TAG, "Credenciales encontradas en FLASH");

    if (g_wifi_type == eWifiEnterprise) {

      ESP_LOGI(TAG, "Cargando WiFi empresarial");

      ret = update_setup_cred(g_wifi_ssid, g_wifi_pass, g_wifi_user,
                              "SETUP_ENTERPRISE_WIFI");

    } else {

      ESP_LOGI(TAG, "Cargando WiFi normal");

      ret = update_setup_cred(g_wifi_ssid, g_wifi_pass, NULL, "SETUP_WIFI");
    }

    ESP_ERROR_CHECK(ret);

    // Conectarse usando las credenciales cargadas
    wifi_init_sta();
  }

  ESP_LOGI(TAG, "ESP CONECTADA A LA RED CON EXITO");
}
esp_err_t save_wifi_credentials(void) {

  char type_str[4];

  snprintf(type_str, sizeof(type_str), "%u", g_wifi_type);

  esp_err_t ret;

  ret = nvs_save_str("wifi_ssid", g_wifi_ssid);
  if (ret != ESP_OK)
    return ret;

  ret = nvs_save_str("wifi_pass", g_wifi_pass);
  if (ret != ESP_OK)
    return ret;

  if (g_wifi_type == eWifiEnterprise) {
    ret = nvs_save_str("wifi_user", g_wifi_user);
    if (ret != ESP_OK)
      return ret;
  }

  return nvs_save_str("wifi_type", type_str);
}

esp_err_t nvs_save_str(const char *key, const char *value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;
  err = nvs_set_str(handle, key, value);
  if (err == ESP_OK)
    nvs_commit(handle);
  nvs_close(handle);
  return err;
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
// ─────────────────────────────────────────────────────────────────────────────
esp_err_t update_setup_cred(char *ssid, char *pswd, char *user, char *type) {

  if (strcmp(type, "SETUP_WIFI") == 0) {
    esp_wifi.ssid = realloc(esp_wifi.ssid, strlen(ssid) + 1);
    esp_wifi.pswd = realloc(esp_wifi.pswd, strlen(pswd) + 1);
    if (esp_wifi.ssid != NULL && esp_wifi.pswd != NULL) {
      strcpy(esp_wifi.ssid, ssid);
      strcpy(esp_wifi.pswd, pswd);
      free(esp_wifi.user_name);
      esp_wifi.user_name = NULL;
      esp_wifi.type_connected = 0;
      return ESP_OK;
    }
    ESP_LOGE(TAG, "");
    return ESP_FAIL;
  } else if (strcmp(type, "SETUP_TCP_CLIENT") == 0) {

  }

  else if (strcmp(type, "ENT") == 0) {
    esp_wifi.ssid = realloc(esp_wifi.ssid, strlen(ssid) + 1);
    esp_wifi.pswd = realloc(esp_wifi.pswd, strlen(pswd) + 1);
    esp_wifi.user_name = realloc(esp_wifi.user_name, strlen(user) + 1);
    if (esp_wifi.ssid != NULL && esp_wifi.pswd != NULL &&
        esp_wifi.user_name != NULL) {
      strcpy(esp_wifi.ssid, ssid);
      strcpy(esp_wifi.pswd, pswd);
      strcpy(esp_wifi.user_name, user);
      esp_wifi.type_connected = 1;
      return ESP_OK;
    }
    uart_write_bytes(UART_NUM_0, "ERR:NO_MEM\n", 11);
    return ESP_FAIL;
  }

  return ESP_FAIL;
}

void tcp_process_task(void *params) {

  TYPE_FORMAT_REQUEST_T *frame;

  // uint8_t buffer[MAX_DATA]; // para mostrar mensajes por UART
  esp_err_t ret;

  uint8_t frame_len;
  // int len;
  int offset = 0;
  while (1) {

    // recibira los datos e por cola
    if (xQueueReceive(tcp_rx_queue, &frame, portMAX_DELAY)) {

      // ACK: identificador ACK y len != 0xFF
      if (frame->header == ACK && frame->len != 0xFF) {

        ESP_LOGI(TAG, "ACK recibido");

        uint8_t data_len = frame->len - 1;

        // entonces cuando viene con ACK de parte de la RPI, trae las
        // credenciales pero aun no estan fromateadas

        // indicando que sera una red normal.
        if (frame->action == eRespWifi) {
          uint8_t *ssid_aux = NULL;
          uint8_t *pswd_aux = NULL;
          uint8_t *user_aux = NULL;

          uint8_t ssid_len = 0;
          uint8_t pswd_len = 0;
          uint8_t user_len = 0;

          uint8_t enterprise_frame =
              0; // 0 indica que no de empresa, si se activa en 1, indica que si
                 // es de empresa la red

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

            if (field_id != eDataSSID && field_id != eDataPSWD) {
              // entonces el frame tiene un error por lo que salidmos e
              // indicamos squi el error
              //@ERROR frame con inicio incorrecto, desechamaos todo el frame
              ESP_LOGE(TAG, "\r\nFrame con ID incorrecto\r\n");
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

              // esta correcto el SSID
              memcpy(ssid_aux, &frame->data[offset], field_len);
              ssid_aux[field_len] = '\0';
              ssid_len = field_len;
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

              memcpy(pswd_aux, &frame->data[offset], field_len);
              pswd_aux[field_len] = '\0';
              pswd_len = field_len;
            } else if (frame->type_frame == eWifiEnterprise) {
              //
              // pero perimo debermos de verificar que antes haya ya estado
              // sando el ssid como el pswd
              if (ssid_aux != NULL && pswd_aux != NULL) {
                // entonces ya tenemos el resto
                if (user_aux != NULL) {
                  ESP_LOGE(TAG, "USER duplicasdo");
                  break;
                }

                if (field_len > USER_LEN_MAX) {
                  ESP_LOGE(TAG, "USER demasiado largo: %u", field_len);
                  break;
                }

                // todo esta correcto
                memcpy(user_aux, &frame->data[offset], field_len);
                user_aux[field_len] = '\0';
                user_len = field_len;
                enterprise_frame = 1;
              }
              continue; // en otro caso si no estan incializados segudimos
                        // ahasta que estos 2 campos esten inicializaods
            }

            offset += field_len; // avanzamos al sigueinte campo
          }

          if (enterprise_frame == 1) {
            ret = update_setup_cred((char *)ssid_aux, (char *)pswd_aux,
                                    (char *)user_aux, "SETUP_ENTERPRISE_WIFI");
          } else {
            ret = update_setup_cred((char *)ssid_aux, (char *)pswd_aux, NULL,
                                    " SETUP_WIFI");
          }
        }

        xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);

        // entonces en este momento ya se tienen las credenicales actualizadas.
        vPortFree(frame);

        // entonces ahora envio a aque se actualice

        continue;
      }
    }

    // NACK: identificador 0x3501 y len == 0xFF
    else if (frame->id == ACK && frame->len == 0xFF) {

      if (login_pending) {
        xEventGroupSetBits(g_login_event_group, LOGIN_FAIL);
        login_pending = 0;
      }

      len = snprintf(buffer, sizeof(buffer), "\r\nservidor contesta: NACK\r\n");
      uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
      uart_write_bytes(global_uart.NUM_PORT, buffer, len);
      uart_write_bytes(global_uart.NUM_PORT, UART_RESET, strlen(UART_RESET));
      vPortFree(frame);
      continue;
    }

    // en otro caso recibimos un comando del servidor (trama CAFE)
    else if (frame->id == HEADER) {
      frame_len = 0;

      // si llego ahora debemos de ver uqe onda

      // verificamos que sea para nosotros
      if (frame->user != user) {
        len = snprintf(buffer, sizeof(buffer),
                       "\r\npeticion no para este usuario\r\n");
        uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
        uart_write_bytes(global_uart.NUM_PORT, buffer, len);
        uart_write_bytes(global_uart.NUM_PORT, UART_RESET, strlen(UART_RESET));
        free(frame);
        continue;
      }

      // ahora necesitamos ver que servicio es lo necesario
      if (frame->action == read_esp) {
        switch (frame->resourse) {
        case led: {
          // el estado esta 1 o 0, que solo abarca 1 byte
          memcpy(send_info.format_request.value, &led_state, 1);
          send_info.format_request.len = 1; // solo usamos 1 byte para el led
          send_info.op_type = OP_ACK;       // operacion ACK
          ret = send_message();
          if (ret != ESP_OK) {
            len = snprintf(buffer, sizeof(buffer),
                           "\r\nERRO AL SER EL ENVIO DEL FRAME\r\n");
            uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
            uart_write_bytes(global_uart.NUM_PORT, buffer, len);
            uart_write_bytes(global_uart.NUM_PORT, UART_RESET,
                             strlen(UART_RESET));
          }

        } break;

        case adc: {
          // adc puede ser un valor de 16 bits
          uint16_t adc_state = read_adc(ADC_CHANNEL);
          uint16_t adc_net = htons(adc_state);
          memcpy(send_info.format_request.value, &adc_net,
                 2); // en este caso usamos 2 bytes
          send_info.format_request.len = 2;
          send_info.op_type = OP_ACK;
          ret = send_message();

          if (ret != ESP_OK) {
            len = snprintf(buffer, sizeof(buffer),
                           "\r\nERRO AL SER EL ENVIO DEL FRAME\r\n");
            uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
            uart_write_bytes(global_uart.NUM_PORT, buffer, len);
            uart_write_bytes(global_uart.NUM_PORT, UART_RESET,
                             strlen(UART_RESET));
          }
        } break;

        case pwm: {
          uint16_t duty = pwm_get_duty();
          uint8_t pct = (uint8_t)(((uint32_t)duty * 100U) / PWM_MAX);
          send_info.format_request.value[0] = pct;
          send_info.format_request.len = 1;
          send_info.op_type = OP_ACK;
          ret = send_message();

          if (ret != ESP_OK) {
            len = snprintf(buffer, sizeof(buffer),
                           "\r\nERRO AL SER EL ENVIO DEL FRAME\r\n");
            uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
            uart_write_bytes(global_uart.NUM_PORT, buffer, len);
            uart_write_bytes(global_uart.NUM_PORT, UART_RESET,
                             strlen(UART_RESET));
          }
        } break;

        default: {
          send_info.op_type = OP_NACK;
          ret = send_message();
        } break;
        }
      } else if (frame->action == write_esp) {
        switch (frame->resourse) {

        case led: {
          // quiere escribir
          led_state = frame->value[0];
          gpio_set_level(OUTPUT_PIN, led_state);

          memcpy(send_info.format_request.value, &led_state, 1);
          send_info.format_request.len = 1;
          send_info.format_request.value[0] = led_state;
          send_info.op_type = OP_ACK;
          // conestamos a la peticion
          ret = send_message();
        } break;

        case pwm: {
          uint8_t pct = frame->value[0];
          if (pct > 100)
            pct = 100;
          uint16_t duty = (uint16_t)(((uint32_t)pct * PWM_MAX) / 100U);
          pwm_set_duty(duty);
          duty = pwm_get_duty();
          pct = (uint8_t)(((uint32_t)duty * 100U) / PWM_MAX);
          send_info.format_request.value[0] = pct;
          send_info.format_request.len = 1;
          send_info.op_type = OP_ACK;
          ret = send_message();
        } break;
        case adc: {
          len = snprintf(buffer, sizeof(buffer),
                         "\r\noperacion con ADC incorrecta\r\n");
          uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
          uart_write_bytes(global_uart.NUM_PORT, buffer, len);
          uart_write_bytes(global_uart.NUM_PORT, UART_RESET,
                           strlen(UART_RESET));

          // enviamos un NACK
          send_info.op_type = OP_NACK;
          ret = send_message();
        } break;

        default: {
          send_info.op_type = OP_NACK;
          ret = send_message();
        } break;
        }
      }

      vPortFree(frame);
    }

    else {
      len = snprintf(buffer, sizeof(buffer), "\r\nFORMTAMO INCORRECTO\r\n");
      uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
      uart_write_bytes(global_uart.NUM_PORT, buffer, len);
      uart_write_bytes(global_uart.NUM_PORT, UART_RESET, strlen(UART_RESET));

      // debemos de enviar un NACK

      send_info.op_type = OP_NACK;
      ret = send_message();
      if (ret != ESP_OK) {
        len = snprintf(buffer, sizeof(buffer),
                       "\r\nERRO AL SER EL ENVIO DEL FRAME\r\n");
        uart_write_bytes(global_uart.NUM_PORT, UART_RED, strlen(UART_RED));
        uart_write_bytes(global_uart.NUM_PORT, buffer, len);
        uart_write_bytes(global_uart.NUM_PORT, UART_RESET, strlen(UART_RESET));
      }
      free(frame);
      continue;
    }
  }
}

void setup_tcp(void) {

  // no es tarea, pero realizara procesos que pueden tardar o llevar mas o menos
  // necetiamos una froma de saber que ya solciitamos, las credenicales WIFI.

  static req_wifi = 0;
  esp_err_t ret;
  while (1) {

    esp_err_t ret = tcp_cliente_init();
    int len;

    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "\r\nconexion con el servidor establecida\r\n");
      xTaskCreate(recv_task, "recv_task", 4098, NULL, 8, &xRecv_task);
      xTaskCreate(tcp_process_task, "tcp_process_task", 4098, NULL, 8,
                  xTcp_process_task);

      if (req_wifi == 0) {
        // quiere decir que aprnas creamos la tarea talvez o que no hemos
        // solicitado las credenicales para coenctarnos a WIFI.
        send_info.op_type = OPReqCredWifi;
        ret = send_message();
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
