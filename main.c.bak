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
#include <driver/uart.h>

// logs
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

// wifi
#include <esp_http_server.h> // este se utiliza, porque cunado no se teine ccredenciales
#include <esp_wifi.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <nvs_flash.h>
// la ESP inica en modo acces point, cunado se tiene guardadas crecencuales en
// flash inica en modo estacion (STA)

// biblioteclas propias
#include "global.h"
#include "modules/DHT11/dht11_lib.h"
#include "modules/HTTP/http_setup.h"
#include "modules/MQTT/mqtt_lib.h"
//#include "modules/UART/uart_lib.h"
#include "modules/WIFI/wifi_lib.h"
// macros

//+++++++++++++ colas
// estas colas las utilice tanto para enviar datos entre modulos como ingresar
// por UART, mediante teclado infromacion esto en el caso se debugear, porque
// despues lo que usaremos para comunicarnos sera MQTT
//QueueHandle_t flow_data_queue;
//QueueHandle_t uart_event;
//QueueHandle_t uart1_tx_queue;

//+++++++++++++ grupos de eventos
EventGroupHandle_t s_wifi_event_group;
esp_wifi_t esp_wifi;

// semaforos?

// variables globales

char g_wifi_ssid[33] = {0};
char g_wifi_pass[65] = {0};
char g_broker_ip[16] = {0}; // solo la IP, luego se arma la URI

// funciones
// este se comenta porque este lo que hace es parsear, separar los tokens por input desde UART, por lo que por ahora no me sirve, 
//char **parse_input(char *line);
esp_err_t update_setup_cred(char *key, char *anchor, char *pswd_ent,
                            char *identificator);

esp_err_t nvs_save_str(const char *key, const char *value);
esp_err_t nvs_load_str(const char *key, char *buf, size_t len);

// tareas

void task_cmd_uart(void *params);

void app_main(void) {

  /**
   * lo primero que tendriamos que hacer es inciar WIFI,
   *
   * */

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Colas
  flow_data_queue = xQueueCreate(10, sizeof(char *));
  uart1_tx_queue = xQueueCreate(5, sizeof(char *));

  // UART0 (debug)
  QueueHandle_t uart0_evt;
  uart_init(UART_NUM_0, 115200, UART_DATA_8_BITS, UART_PARITY_DISABLE,
            UART_STOP_BITS_1, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
            &uart0_evt);
  uart_task_config_t uart0_cfg = {.port = UART_NUM_0, .event_queue = uart0_evt};
  xTaskCreate(task_uart, "task_uart", 4096, &uart0_cfg, 9, NULL);

  // UART1 (solo usado por master para SET_WIFI)
  QueueHandle_t uart1_evt;
  uart_init(UART_NUM_1, 115200, UART_DATA_8_BITS, UART_PARITY_DISABLE,
            UART_STOP_BITS_1, 18, 19, &uart1_evt);
  uart_task_config_t uart1_cfg = {.port = UART_NUM_1, .event_queue = uart1_evt};
  xTaskCreate(task_uart, "task_uart1", 4096, &uart1_cfg, 8, NULL);

  xTaskCreate(task_cmd_uart, "task_cmd_uart", 4096, NULL, 8, NULL);

  // Grupo de eventos WiFi
  s_wifi_event_group = xEventGroupCreate();

  // --- Carga/obtencion de credenciales WiFi
  bool creds_in_nvs = false;
  if (nvs_load_str("wifi_ssid", g_wifi_ssid, sizeof(g_wifi_ssid)) == ESP_OK &&
      nvs_load_str("wifi_pass", g_wifi_pass, sizeof(g_wifi_pass)) == ESP_OK) {
    creds_in_nvs = true;
  }

  if (!creds_in_nvs) {
    // AP + página web
    char msg[] = "\r\n[WIFI] Sin credenciales guardadas. Iniciando punto de "
                 "acceso...\r\n";
    uart_write_bytes(UART_NUM_0, msg, sizeof(msg) - 1);
    wifi_init_ap();
    start_http_setup_server(s_wifi_event_group);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CREDS_RECEIVED, pdTRUE,
                        pdFALSE, portMAX_DELAY);
    stop_http_setup_server();
    wifi_stop_ap();
  }

  // Conectar a WiFi en modo estación
  update_setup_cred(g_wifi_ssid, g_wifi_pass, NULL, "SSID");
  xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);
  wifi_init_sta();
  // Restaurar el bit que fue borrado en wifi_init_sta()
  xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

// NVS helpers

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
  nvs_close(handle) return err;
}

void task_cmd_uart(void *params) {
  char *cmd_receive;
  char error_msg[120];
  int len;

  while (1) {
    if (xQueueReceive(flow_data_queue, &cmd_receive, portMAX_DELAY)) {

      char **tokens = parse_input(cmd_receive);

      if (tokens == NULL || tokens[0] == NULL) {
        len = snprintf(error_msg, sizeof(error_msg),
                       "\r\nMAIN -> ERR: No se recibieron comandos\r\n");
        uart_write_bytes(UART_NUM_0, error_msg, len);
        free(cmd_receive);
        continue;
      }

      if (strcmp(tokens[0], "SET_WIFI") == 0) {
        if (tokens[1] == NULL) {
          len = snprintf(error_msg, sizeof(error_msg),
                         "\r\nMAIN: error al parsear\r\n");
          uart_write_bytes(UART_NUM_0, error_msg, len);
        } else {
          if (tokens[2] == NULL) {
            len = snprintf(error_msg, sizeof(error_msg),
                           "\r\nMAIN -> ERR: formato incorrecto\r\n");
            uart_write_bytes(UART_NUM_0, error_msg, len);
          } else if (update_setup_cred(tokens[1], tokens[2], NULL, "SSID") ==
                     ESP_OK) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);
          } else {
            len = snprintf(error_msg, sizeof(error_msg),
                           "\r\nMAIN -> ERR: no memoria\r\n");
            uart_write_bytes(UART_NUM_0, error_msg, len);
          }
        }
      }

      // ── ENT_WIFI ──────────────────────────────────────────────────────
      else if (strcmp(tokens[0], "ENT_WIFI") == 0) {
        if (tokens[1] == NULL) {
          len = snprintf(error_msg, sizeof(error_msg),
                         "\r\nMAIN -> ERR: formato incorrecto\r\n");
          uart_write_bytes(UART_NUM_0, error_msg, len);
        } else if (strncmp(tokens[1], "DFT_ENT_", 8) == 0) {
          int found = 0;
          for (int i = 0; i < (int)DEFAULT_ENT_PROFILES_COUNT; i++) {
            if (strcmp(tokens[1], default_ent_profiles[i].alias) == 0) {
              found = 1;
              if (update_setup_cred((char *)default_ent_profiles[i].ssid,
                                    (char *)default_ent_profiles[i].pswd,
                                    (char *)default_ent_profiles[i].user,
                                    "ENT") == ESP_OK)
                xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);
              else {
                len = snprintf(error_msg, sizeof(error_msg),
                               "\r\nMAIN: error credenciales\r\n");
                uart_write_bytes(UART_NUM_0, error_msg, len);
              }
              break;
            }
          }
          if (!found) {
            len = snprintf(error_msg, sizeof(error_msg),
                           "\r\nMAIN: perfil ENT no encontrado\r\n");
            uart_write_bytes(UART_NUM_0, error_msg, len);
          }
        } else {
          if (tokens[2] == NULL || tokens[3] == NULL) {
            len = snprintf(error_msg, sizeof(error_msg),
                           "\r\nMAIN -> ERR: formato incorrecto\r\n");
            uart_write_bytes(UART_NUM_0, error_msg, len);
          } else if (update_setup_cred(tokens[1], tokens[3], tokens[2],
                                       "ENT") == ESP_OK) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CREDS_READY);
          } else {
            len = snprintf(error_msg, sizeof(error_msg),
                           "\r\nMAIN -> ERR: no memoria\r\n");
            uart_write_bytes(UART_NUM_0, error_msg, len);
          }
        }
      }

      free(cmd_receive);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_input
// ─────────────────────────────────────────────────────────────────────────────
char **parse_input(char *line) {
  char **tokens = malloc(6 * sizeof(char *));
  char *token;
  int position = 0;

  token = strtok(line, ":");
  while (token != NULL) {
    if (position >= 5)
      break;
    tokens[position++] = strdup(token);
    token = strtok(NULL, ":");
  }
  tokens[position] = NULL;
  return tokens;
}

// ─────────────────────────────────────────────────────────────────────────────
// update_setup_cred
// ─────────────────────────────────────────────────────────────────────────────
esp_err_t update_setup_cred(char *ssid, char *pswd, char *user, char *type) {
  if (strcmp(type, "SSID") == 0) {
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
    uart_write_bytes(UART_NUM_0, "ERR:NO_MEM\n", 11);
    return ESP_FAIL;
  }

  if (strcmp(type, "ENT") == 0) {
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
