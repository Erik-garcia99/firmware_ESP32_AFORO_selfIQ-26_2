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
#include "modules/MQTT/mqtt_lib.h"
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
static selfiq_broker_t mqtt_broker;

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

/* WiFi y MQTT son etapas independientes. El snapshot conserva la red final
 * incluso si falla NVS o si temporalmente se consulta el TCP del AP. */
static selfiq_credentials_t verified_wifi;

static void remember_verified_wifi(void) {
  memset(&verified_wifi, 0, sizeof(verified_wifi));
  snprintf(verified_wifi.ssid, sizeof(verified_wifi.ssid), "%s", esp_wifi.ssid);
  snprintf(verified_wifi.password, sizeof(verified_wifi.password), "%s", esp_wifi.pswd);
  verified_wifi.kind = esp_wifi.type_connected == 1 ? eWifiEnterprise : eWifiDefault;
  if (verified_wifi.kind == eWifiEnterprise)
    snprintf(verified_wifi.username, sizeof(verified_wifi.username), "%s", esp_wifi.user_name);
}

static esp_err_t restore_verified_wifi(void) {
  return update_setup_cred(verified_wifi.ssid, verified_wifi.password,
      verified_wifi.kind == eWifiEnterprise ? verified_wifi.username : NULL,
      verified_wifi.kind == eWifiEnterprise ? "SETUP_ENTERPRISE_WIFI" : "SETUP_WIFI");
}

/* Al cambiar de red no reutilizar un broker antiguo de otra instalacion.
 * Se preserva node_id: borrar el destino no cambia la identidad del nodo. */
static esp_err_t invalidate_saved_broker(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("selfiq_mqtt", NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  const char *keys[] = {"broker_ip", "broker_port"};
  for (unsigned i = 0; i < 2 && err == ESP_OK; ++i) {
    err = nvs_erase_key(handle, keys[i]);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  }
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

/* Se llama SOLO tras confirmar WiFi definitivo e IP. Por ahora el unico
 * servidor de descubrimiento conocido esta en el AP de configuracion.
 * No solicita ni vuelve a guardar credenciales WiFi durante esta consulta. */
static esp_err_t request_broker_after_wifi(bool *final_connected) {
  *final_connected = false;
  ESP_LOGI(TAG, "ETAPA BROKER: Wi-Fi definitivo ya confirmado; consulta TCP separada");
  selfiq_tcp_close();
  esp_err_t err = update_setup_cred(SELFIQ_SETUP_SSID, SELFIQ_SETUP_PASSWORD,
                                    NULL, "SETUP_WIFI");
  selfiq_broker_t candidate = {0};
  if (err == ESP_OK && !connect_current("AP SOLO PARA CONSULTAR BROKER"))
    err = ESP_FAIL;
  if (err == ESP_OK) err = update_tcp_config(RPI_IP_SETUP, RPI_PORT_SETUP);
  if (err == ESP_OK) err = tcp_cliente_init();
  if (err == ESP_OK) err = tcp_request_broker(&candidate);
  selfiq_tcp_close();
  /* Restaurar siempre, incluso tras NACK, timeout o AP ausente. */
  esp_err_t restore = restore_verified_wifi();
  if (restore != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo restaurar Wi-Fi en RAM: %s", esp_err_to_name(restore));
    return restore;
  }
  *final_connected = connect_current("REGRESO A RED DEFINITIVA VERIFICADA");
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Broker pendiente: %s. Wi-Fi se conserva; no se piden credenciales otra vez",
             esp_err_to_name(err));
    return err;
  }
  if (!*final_connected) return ESP_ERR_INVALID_STATE;
  err = update_mqtt_config(candidate.ip, candidate.port);
  if (err != ESP_OK) return err;
  mqtt_broker = candidate;
  err = mqtt_save_broker();
  if (err != ESP_OK)
    ESP_LOGW(TAG, "Broker valido en RAM, pero no se guardo en NVS: %s", esp_err_to_name(err));
  return ESP_OK;
}

void app_main(void) {
  ESP_LOGI(TAG, "SelfIQ: primero Wi-Fi definitivo e IP; despues broker/MQTT");
  s_wifi_event_group = xEventGroupCreate();
  g_tcp_event_group = xEventGroupCreate();
  if (!s_wifi_event_group || !g_tcp_event_group) {
    ESP_LOGE(TAG, "Sin memoria para grupos de eventos");
    return;
  }
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS incompatible/llena; se reinicializara");
    e = nvs_flash_erase();
    if (e == ESP_OK) e = nvs_flash_init();
  }
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "No pudo iniciar NVS: %s", esp_err_to_name(e));
    return;
  }

  bool connected = false;
  if (load_wifi_credentials() == ESP_OK) {
    connected = connect_current("RED GUARDADA");
    if (connected) remember_verified_wifi();
  }
  bool broker_ready = mqtt_load_broker(&mqtt_broker) == ESP_OK;
  if (!broker_ready)
    ESP_LOGW(TAG, "Sin broker guardado; esto no invalida las credenciales Wi-Fi");

  unsigned cycle = 0;
  while (1) {
    if (connected && esp_wifi.connected) {
      ESP_LOGI(TAG, "WIFI LISTO: red definitiva e IP confirmadas SSID=\"%s\"", esp_wifi.ssid);
      bool mqtt_started = false;
      bool broker_attempted = false;
      TickType_t last_broker_attempt = 0;
      while (connected && esp_wifi.connected) {
        TickType_t now = xTaskGetTickCount();
        if (!broker_ready && (!broker_attempted ||
            (TickType_t)(now - last_broker_attempt) >= pdMS_TO_TICKS(60000))) {
          e = request_broker_after_wifi(&connected);
          broker_ready = e == ESP_OK;
          broker_attempted = true;
          last_broker_attempt = xTaskGetTickCount();
          if (!connected) break;
          if (!broker_ready)
            ESP_LOGW(TAG, "Wi-Fi listo, MQTT pendiente. Reintento SOLO de broker en 60 s");
        }
        if (broker_ready && !mqtt_started) {
          e = mqtt_init(s_wifi_event_group, mqtt_broker.ip);
          mqtt_started = e == ESP_OK;
          if (!mqtt_started) {
            ESP_LOGW(TAG, "MQTT no inicio: %s; Wi-Fi sigue valido", esp_err_to_name(e));
            vTaskDelay(pdMS_TO_TICKS(5000));
          }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      e = mqtt_stop();
      if (e != ESP_OK) {
        ESP_LOGE(TAG, "No pudo detenerse MQTT: %s", esp_err_to_name(e));
        return;
      }
      /* Si falla la RAM, no intentar conectar al AP como si fuera red final. */
      e = restore_verified_wifi();
      if (e != ESP_OK) {
        ESP_LOGE(TAG, "Restaurar red definitiva fallo: %s", esp_err_to_name(e));
        return;
      }
      connected = connect_current("RECONEXION RED DEFINITIVA");
      if (connected) continue;
      ESP_LOGW(TAG, "La red definitiva no responde; se solicitara Wi-Fi al AP");
    }

    connected = false;
    ++cycle;
    ESP_LOGI(TAG, "=== APROVISIONAMIENTO WIFI %u ===", cycle);
    selfiq_tcp_close();
    e = update_setup_cred(SELFIQ_SETUP_SSID, SELFIQ_SETUP_PASSWORD, NULL, "SETUP_WIFI");
    if (e == ESP_OK && connect_current("AP RASPBERRY PARA WIFI")) {
      e = setup_tcp(); /* Solo recibir/aplicar WiFi. No consulta el broker. */
      if (e == ESP_OK) {
        connected = connect_current("CONFIRMAR RED DEFINITIVA ANTES DEL BROKER");
        if (connected) {
          remember_verified_wifi();
          broker_ready = false;
          /* Antes de persistir una red nueva, quitar el destino de la anterior. */
          e = invalidate_saved_broker();
          if (e == ESP_OK) e = save_wifi_credentials();
          if (e == ESP_OK)
            ESP_LOGI(TAG, "WIFI CONFIRMADO Y GUARDADO EN NVS. Ahora puede iniciar etapa broker");
          else
            ESP_LOGW(TAG, "Wi-Fi confirmado en RAM; NVS no actualizada: %s", esp_err_to_name(e));
          continue;
        }
        ESP_LOGW(TAG, "La nueva red no conecto: no se reemplazan credenciales NVS");
      }
    }
    ESP_LOGW(TAG, "Wi-Fi no completado; reintento de aprovisionamiento en 5 s");
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
