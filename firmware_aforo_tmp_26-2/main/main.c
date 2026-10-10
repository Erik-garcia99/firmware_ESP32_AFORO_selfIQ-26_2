/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global.h"
#include "modules/TCP/tcp_lib.h"
#include "modules/WIFI/wifi_lib.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//+++++++++++++++++++++++ grupos de eventos
EventGroupHandle_t s_wifi_event_group;
EventGroupHandle_t g_tcp_event_group;
// ++++++++++++++++++++ colas
QueueHandle_t tcp_rx_queue;

//++++++++++++++++++++++++ estrucutras y enums
TYPE_ESP_WIFI_T esp_wifi = {0};
TYPE_TCP_CLIENT_T tcp_client = {.sock = -1};
TYPE_OP_TYPE_T op_type;
ACTION_T action;
TYPE_FRAME_T type_frame;
DATA_FIELD_T data_field_t;
TYPE_SEND_INFO_T send_info = {0};
TYPE_FORMAT_REQUEST_T format_request = {0};
//+++++++++++++++++++++++++++ variables
uint8_t g_wifi_type = eWifiDefault;
static const char *TAG = "MAIN_AFORO";

//+++++++++++++++++++++++++ funciones
esp_err_t nvs_load_str(const char *key, char *buf, size_t len);
esp_err_t save_wifi_credentials(void);
esp_err_t load_wifi_credentials(void);
esp_err_t update_setup_cred(char *ssid, char *pass, char *user, char *type);
static esp_err_t setup_tcp(void);

static bool connect_current(const char *stage) {
  ESP_LOGI(TAG, "%s: conectando SSID=\"%s\"", stage, esp_wifi.ssid);
  esp_err_t e = wifi_connect_current(WIFI_ATTEMPTS, WIFI_ATTEMPT_TIMEOUT_MS);
  if (e != ESP_OK || !esp_wifi.connected) {
    if (e == ESP_OK)
      e = ESP_ERR_INVALID_STATE;
    ESP_LOGE(TAG, "%s fallo: %s (0x%x)", stage, esp_err_to_name(e),
             (unsigned)e);
    return false;
  }
  ESP_LOGI(TAG, "%s completado", stage);
  return true;
}

static esp_err_t setup_tcp(void) {
  ESP_LOGI(TAG, "PASO 3: preparando TCP hacia Raspberry %s:%u", RPI_IP_SETUP,
           RPI_PORT_SETUP);
  esp_err_t e = update_tcp_config(RPI_IP_SETUP, RPI_PORT_SETUP);
  if (e != ESP_OK)
    return e;
  e = tcp_cliente_init();
  if (e != ESP_OK)
    return e;
  selfiq_credentials_t credentials;
  ESP_LOGI(TAG, "PASO 4: solicitando credenciales de red definitiva");
  e = tcp_request_wifi(&credentials);
  selfiq_tcp_close();
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "Solicitud de credenciales fallo: %s (0x%x)",
             esp_err_to_name(e), (unsigned)e);
    return e;
  }
  ESP_LOGI(TAG, "PASO 5: actualizando credenciales en RAM, SSID=\"%s\"",
           credentials.ssid);
  e = update_setup_cred(
      credentials.ssid, credentials.password,
      credentials.kind == eWifiEnterprise ? credentials.username : NULL,
      credentials.kind == eWifiEnterprise ? "SETUP_ENTERPRISE_WIFI"
                                          : "SETUP_WIFI");
  if (e != ESP_OK)
    ESP_LOGE(TAG, "No pudo actualizarse RAM: %s", esp_err_to_name(e));
  return e;
}

void app_main(void) {
  ESP_LOGI(TAG,
           "SelfIQ provisioning diagnostico v2: inicio (passwords ocultos)");
  s_wifi_event_group = xEventGroupCreate();
  g_tcp_event_group = xEventGroupCreate();
  if (!s_wifi_event_group || !g_tcp_event_group) {
    ESP_LOGE(TAG, "Sin memoria para grupos de eventos; no puede iniciarse");
    return;
  }
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS necesita reinicializacion: %s; se borrara NVS",
             esp_err_to_name(e));
    e = nvs_flash_erase();
    if (e == ESP_OK)
      e = nvs_flash_init();
  }
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "No pudo iniciar NVS: %s", esp_err_to_name(e));
    return;
  }

  ESP_LOGI(TAG, "PASO 1: buscando credenciales guardadas en NVS");
  e = load_wifi_credentials();
  bool connected = false;
  if (e == ESP_OK) {
    ESP_LOGI(TAG, "NVS contiene red SSID=\"%s\"; se probara antes del AP",
             esp_wifi.ssid);
    connected = connect_current("RED GUARDADA");
    if (!connected)
      ESP_LOGW(TAG, "Red guardada no conecta; entrando a aprovisionamiento "
                    "(NVS se conserva)");
  } else {
    ESP_LOGW(TAG, "Sin credenciales NVS completas/utilizables: %s (0x%x)",
             esp_err_to_name(e), (unsigned)e);
  }

  unsigned cycle = 0;
  while (1) {
    if (connected && esp_wifi.connected) {
      ESP_LOGI(TAG,
               "LISTO: ESP32 conectada a red definitiva SSID=\"%s\". TCP de "
               "setup cerrado",
               esp_wifi.ssid);
      /* Current uploaded firmware starts no DHT/MQTT tasks. Add your sensor
       * task startup here once, if required by the rest of the project. */
      while (esp_wifi.connected)
        vTaskDelay(pdMS_TO_TICKS(1000));
      ESP_LOGW(TAG, "Se perdio la red definitiva; reintentando la misma red");
      connected = connect_current("RECONEXION RED DEFINITIVA");
      if (connected)
        continue;
      ESP_LOGW(TAG, "No recupero la red definitiva; buscara SelfIQ-Admi");
    }
    connected = false;
    ++cycle;
    ESP_LOGI(TAG, "=== CICLO DE APROVISIONAMIENTO %u ===", cycle);
    ESP_LOGI(TAG, "PASO 2: configurar y conectar AP Raspberry SSID=\"%s\"",
             SELFIQ_SETUP_SSID);
    selfiq_tcp_close();
    e = update_setup_cred(SELFIQ_SETUP_SSID, SELFIQ_SETUP_PASSWORD, NULL,
                          "SETUP_WIFI");
    if (e != ESP_OK) {
      ESP_LOGE(TAG, "Configurar AP en RAM fallo: %s", esp_err_to_name(e));
    } else if (connect_current("AP RASPBERRY")) {
      e = setup_tcp();
      if (e == ESP_OK) {
        ESP_LOGI(
            TAG,
            "PASO 6: dejando AP y conectando red definitiva de la Raspberry");
        connected = connect_current("RED DEFINITIVA");
        if (connected) {
          ESP_LOGI(TAG, "PASO 7: guardando credenciales verificadas en NVS");
          e = save_wifi_credentials();
          if (e == ESP_OK)
            ESP_LOGI(TAG,
                     "NVS guardada. Proximo arranque usara la red definitiva");
          else
            ESP_LOGE(TAG,
                     "Wi-Fi conectado pero NVS no se guardo: %s; al reiniciar "
                     "requerira nueva configuracion",
                     esp_err_to_name(e));
          continue;
        }
        ESP_LOGW(TAG, "Credenciales recibidas no lograron conectar; no se "
                      "guardan en NVS");
      }
    }
    ESP_LOGW(TAG, "Ciclo no completado. Reintento en 5 segundos; si AP no "
                  "existe, reabrirlo desde Raspberry");
    vTaskDelay(pdMS_TO_TICKS(5000));
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

esp_err_t update_setup_cred(char *ssid, char *pass, char *user, char *type) {
  bool enterprise = type && strcmp(type, "SETUP_ENTERPRISE_WIFI") == 0;
  if (!ssid || !pass || !type || !ssid[0] || strlen(ssid) > SSID_LEN_MAX ||
      strlen(pass) > 64 ||
      (!enterprise &&
       (strlen(pass) < PSWD_LEN_MIN || strlen(pass) > PSWD_LEN_MAX)) ||
      (enterprise && (!user || !user[0] || strlen(user) > USER_LEN_MAX)) ||
      (!enterprise && strcmp(type, "SETUP_WIFI") != 0))
    return ESP_ERR_INVALID_ARG;
  char *s = malloc(strlen(ssid) + 1);
  char *p = malloc(strlen(pass) + 1);
  char *u = enterprise ? malloc(strlen(user) + 1) : NULL;
  if (!s || !p || (enterprise && !u)) {
    free(s);
    free(p);
    free(u);
    return ESP_ERR_NO_MEM;
  }
  strcpy(s, ssid);
  strcpy(p, pass);
  if (enterprise)
    strcpy(u, user);
  free(esp_wifi.ssid);
  free(esp_wifi.pswd);
  free(esp_wifi.user_name);
  esp_wifi.ssid = s;
  esp_wifi.pswd = p;
  esp_wifi.user_name = u;
  esp_wifi.type_connected = enterprise ? 1 : 0;
  g_wifi_type = enterprise ? eWifiEnterprise : eWifiDefault;
  return ESP_OK;
}
