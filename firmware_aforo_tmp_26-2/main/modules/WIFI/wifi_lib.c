/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor erik garcia chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 * ************************************************* */

/**
 * SelfIQ - Cliente WiFi ESP32 (solo modo STA).
 * Soporta WPA2-Personal y WPA-Enterprise mediante esp_eap_client.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "esp_eap_client.h"
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "wifi_lib.h"

#ifndef ESP_MAX_RETRY
#define ESP_MAX_RETRY 5
#endif

#define WIFI_MANUAL_DISCONNECT_BIT BIT10
#define WIFI_DISCONNECT_TIMEOUT_MS 5000

static const char *TAG = "wifi_lib";
static bool wifi_initialized = false;
static volatile bool manual_disconnect = false;
static int s_retry_num = 0;
static esp_ip4_addr_t last_ip = {0};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi.connected = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    if (manual_disconnect) {
      xEventGroupSetBits(s_wifi_event_group, WIFI_MANUAL_DISCONNECT_BIT);
      return;
    }

    if (s_retry_num < ESP_MAX_RETRY) {
      s_retry_num++;
      ESP_LOGW(TAG, "Reintentando conexion WiFi (%d/%d)", s_retry_num,
               ESP_MAX_RETRY);
      esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al reintentar WiFi: %s", esp_err_to_name(err));
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      }
    } else {
      ESP_LOGE(TAG, "Se agotaron los reintentos WiFi");
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
    last_ip = event->ip_info.ip;
    esp_wifi.ip = &last_ip;
    esp_wifi.connected = 1;
    s_retry_num = 0;
    ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&last_ip));
    xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void) {
  if (s_wifi_event_group == NULL || esp_wifi.ssid == NULL ||
      esp_wifi.pswd == NULL || esp_wifi.ssid[0] == '\0') {
    ESP_LOGE(TAG, "Grupo de eventos o credenciales WiFi invalidas");
    return;
  }
  if (strlen(esp_wifi.ssid) > 32 || strlen(esp_wifi.pswd) > 64) {
    ESP_LOGE(TAG, "Longitud de SSID o password invalida");
    return;
  }
  if (esp_wifi.type_connected == 1 &&
      (esp_wifi.user_name == NULL || esp_wifi.user_name[0] == '\0')) {
    ESP_LOGE(TAG, "Falta el usuario Enterprise");
    return;
  }

  if (!wifi_initialized) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    if (sta == NULL) {
      ESP_LOGE(TAG, "No se pudo crear interfaz STA");
      return;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_initialized = true;
  }

  wifi_config_t config = {0};
  memcpy(config.sta.ssid, esp_wifi.ssid, strlen(esp_wifi.ssid));

  if (esp_wifi.type_connected == 1) {
    ESP_LOGI(TAG, "Configurando red Enterprise");
    esp_eap_client_clear_identity();
    esp_eap_client_clear_username();
    esp_eap_client_clear_password();
    ESP_ERROR_CHECK(esp_eap_client_set_identity(
        (const uint8_t *)esp_wifi.user_name, strlen(esp_wifi.user_name)));
    ESP_ERROR_CHECK(esp_eap_client_set_username(
        (const uint8_t *)esp_wifi.user_name, strlen(esp_wifi.user_name)));
    ESP_ERROR_CHECK(esp_eap_client_set_password((const uint8_t *)esp_wifi.pswd,
                                                strlen(esp_wifi.pswd)));
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
  } else {
    ESP_LOGI(TAG, "Configurando red Personal");
    memcpy(config.sta.password, esp_wifi.pswd, strlen(esp_wifi.pswd));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_disable());
  }

  s_retry_num = 0;
  esp_wifi.connected = 0;
  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
  ESP_ERROR_CHECK(esp_wifi_connect());

  ESP_LOGI(TAG, "Esperando conexion WiFi e IP...");
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Conexion WiFi exitosa");
  } else {
    ESP_LOGE(TAG, "No se pudo conectar a WiFi");
  }
}

void wifi_reconnect(void) {
  if (!wifi_initialized) {
    wifi_init_sta();
    return;
  }

  ESP_LOGI(TAG, "Cambio intencional de red WiFi");
  manual_disconnect = true;
  xEventGroupClearBits(s_wifi_event_group, WIFI_MANUAL_DISCONNECT_BIT |
                                               WIFI_CONNECTED_BIT |
                                               WIFI_FAIL_BIT);
  esp_wifi.connected = 0;

  esp_err_t err = esp_wifi_disconnect();
  if (err != ESP_OK) {
    manual_disconnect = false;
    ESP_LOGE(TAG, "No se pudo desconectar WiFi: %s", esp_err_to_name(err));
    return;
  }

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_MANUAL_DISCONNECT_BIT, pdTRUE, pdFALSE,
      pdMS_TO_TICKS(WIFI_DISCONNECT_TIMEOUT_MS));
  if (!(bits & WIFI_MANUAL_DISCONNECT_BIT)) {
    ESP_LOGE(TAG, "Timeout esperando desconexion WiFi");
    // No se puede garantizar que la desconexion haya terminado.
    return;
  }

  manual_disconnect = false;
  wifi_init_sta();
}
