/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor Erik Garcia Chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 *
 ************************************************* */

#include "wifi_lib.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

#define WIFI_STOPPED_BIT BIT10
#define WIFI_STOP_TIMEOUT_MS 5000

static const char *TAG = "SELFIQ_WIFI";
static bool initialized = false;
static volatile bool intentional_stop = false;
static volatile bool attempt_active = false;
static volatile unsigned attempt_number = 0;
static volatile unsigned last_reason = 0;
static esp_ip4_addr_t last_ip;

static const char *reason_name(unsigned reason) {
  switch (reason) {
  case WIFI_REASON_NO_AP_FOUND:
    return "AP no encontrado: SSID, cobertura, AP apagado o canal";
  case WIFI_REASON_AUTH_FAIL:
    return "Fallo de autenticacion: clave/metodo/politica AP";
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    return "Timeout handshake WPA (clave incorrecta o enlace inestable)";
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
    return "Timeout handshake (clave/metodo/enlace)";
  case WIFI_REASON_BEACON_TIMEOUT:
    return "Perdida de beacons: AP apagado, cambio canal o senal";
  case WIFI_REASON_ASSOC_FAIL:
    return "AP rechazo asociacion";
  default:
    return "Consulte reason numerico ESP-IDF; no permite asegurar una sola "
           "causa";
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "Interfaz STA iniciada");
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
    esp_wifi.connected = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    xEventGroupSetBits(s_wifi_event_group, WIFI_STOPPED_BIT);
    ESP_LOGI(TAG, "Interfaz STA detenida para cambio/reintento");
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
    const wifi_event_sta_connected_t *e = data;
    ESP_LOGI(TAG, "Asociado al AP, canal=%u; esperando DHCP/IP", e->channel);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *e = data;
    esp_wifi.connected = 0;
    last_reason = e->reason;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    if (intentional_stop) {
      ESP_LOGI(TAG,
               "Desconexion intencional (cambio/reintento de red), reason=%u",
               e->reason);
    } else {
      ESP_LOGW(TAG, "Desconexion/fallo, intento=%u reason=%u: %s",
               attempt_number, e->reason, reason_name(e->reason));
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    /* The main task owns retries; no reconnect loop inside the event handler.
     */
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    if (intentional_stop || !attempt_active)
      return;
    const ip_event_got_ip_t *e = data;
    last_ip = e->ip_info.ip;
    esp_wifi.ip = &last_ip;
    esp_wifi.connected = 1;
    last_reason = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    ESP_LOGI(TAG, "IP=" IPSTR " mascara=" IPSTR " gateway=" IPSTR,
             IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.netmask),
             IP2STR(&e->ip_info.gw));
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
    esp_wifi.connected = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    if (!intentional_stop) {
      ESP_LOGW(TAG, "Se perdio la direccion IP");
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  }
}

static esp_err_t checked(esp_err_t err, const char *step) {
  if (err != ESP_OK)
    ESP_LOGE(TAG, "%s: %s (0x%x)", step, esp_err_to_name(err), (unsigned)err);
  return err;
}
#define CHECK(call)                                                            \
  do {                                                                         \
    esp_err_t e = checked((call), #call);                                      \
    if (e != ESP_OK)                                                           \
      return e;                                                                \
  } while (0)

static esp_err_t init_driver(void) {
  if (initialized)
    return ESP_OK;
  if (!s_wifi_event_group)
    return ESP_ERR_INVALID_STATE;
  ESP_LOGI(TAG, "Inicializando TCP/IP, eventos y driver Wi-Fi");
  CHECK(esp_netif_init());
  esp_err_t e = esp_event_loop_create_default();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
    return checked(e, "esp_event_loop_create_default");
  if (!esp_netif_create_default_wifi_sta()) {
    ESP_LOGE(TAG, "No pudo crearse interfaz esp-netif STA");
    return ESP_ERR_NO_MEM;
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  CHECK(esp_wifi_init(&cfg));
  CHECK(
      esp_wifi_set_storage(WIFI_STORAGE_RAM)); /* Never persist setup AP as
                                                  driver Wi-Fi configuration. */
  CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   wifi_event_handler, NULL));
  CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                   wifi_event_handler, NULL));
  CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  /* Start in wifi_connect_current, after configuration is ready. */
  initialized = true;
  return ESP_OK;
}

/* Stopping the driver bounds old connection attempts and drains STA_STOP
 * before another profile/attempt starts. No shared manual_disconnect stays
 * true. */
static bool driver_running = false;
static esp_err_t stop_driver(void) {
  if (!driver_running)
    return ESP_OK;
  intentional_stop = true;
  attempt_active = false;
  xEventGroupClearBits(s_wifi_event_group, WIFI_STOPPED_BIT);
  esp_err_t e = esp_wifi_stop();
  if (e != ESP_OK) {
    intentional_stop = false;
    return checked(e, "esp_wifi_stop");
  }
  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_event_group, WIFI_STOPPED_BIT, pdTRUE, pdFALSE,
                          pdMS_TO_TICKS(WIFI_STOP_TIMEOUT_MS));
  driver_running = false;
  esp_wifi.connected = 0;
  intentional_stop = false;
  if (!(bits & WIFI_STOPPED_BIT)) {
    ESP_LOGE(
        TAG,
        "Timeout esperando WIFI_EVENT_STA_STOP; no se inicia otro intento");
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

esp_err_t wifi_connect_current(unsigned attempts, unsigned timeout_ms) {
  if (!attempts || !timeout_ms || !esp_wifi.ssid || !esp_wifi.pswd ||
      !esp_wifi.ssid[0])
    return checked(ESP_ERR_INVALID_ARG, "Credenciales/tiempos Wi-Fi");
  if (strlen(esp_wifi.ssid) > 32 || strlen(esp_wifi.pswd) > 64 ||
      (esp_wifi.type_connected != 0 && esp_wifi.type_connected != 1) ||
      (esp_wifi.type_connected == 1 &&
       (!esp_wifi.user_name || !esp_wifi.user_name[0])))
    return checked(ESP_ERR_INVALID_ARG, "Limites de credenciales Wi-Fi");
  CHECK(init_driver());
  CHECK(stop_driver());
  wifi_config_t cfg = {0};
  memcpy(cfg.sta.ssid, esp_wifi.ssid, strlen(esp_wifi.ssid));
  if (esp_wifi.type_connected == 1) {
    ESP_LOGI(TAG, "Configurando Enterprise (usuario/password ocultos)");
    esp_eap_client_clear_identity();
    esp_eap_client_clear_username();
    esp_eap_client_clear_password();
    CHECK(esp_eap_client_set_identity((const uint8_t *)esp_wifi.user_name,
                                      strlen(esp_wifi.user_name)));
    CHECK(esp_eap_client_set_username((const uint8_t *)esp_wifi.user_name,
                                      strlen(esp_wifi.user_name)));
    CHECK(esp_eap_client_set_password((const uint8_t *)esp_wifi.pswd,
                                      strlen(esp_wifi.pswd)));
    /* Existing Enterprise behavior retained. Configure CA/server validation
     * for your institutional network before using it in production. */
    CHECK(esp_wifi_sta_enterprise_enable());
  } else {
    ESP_LOGI(TAG, "Configurando WPA2 Personal (password oculto)");
    memcpy(cfg.sta.password, esp_wifi.pswd, strlen(esp_wifi.pswd));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    CHECK(esp_wifi_sta_enterprise_disable());
  }
  CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  for (unsigned attempt = 1; attempt <= attempts; ++attempt) {
    CHECK(stop_driver());
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    last_reason = 0;
    attempt_number = attempt;
    attempt_active = true;
    esp_wifi.connected = 0;
    ESP_LOGI(TAG, "Intento Wi-Fi %u/%u SSID=\"%s\"; limite=%u ms", attempt,
             attempts, esp_wifi.ssid, timeout_ms);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
      attempt_active = false;
      return checked(err, "esp_wifi_start");
    }
    driver_running = true;
    err = esp_wifi_connect();
    if (err != ESP_OK) {
      checked(err, "esp_wifi_connect");
      attempt_active = false;
    } else {
      TickType_t start = xTaskGetTickCount();
      TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
      for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout)
          break;
        TickType_t remaining = timeout - elapsed;
        TickType_t slice = pdMS_TO_TICKS(5000);
        if (slice > remaining)
          slice = remaining;
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,
            pdFALSE, slice);
        if ((bits & WIFI_CONNECTED_BIT) && esp_wifi.connected) {
          ESP_LOGI(TAG, "Wi-Fi e IP confirmados para \"%s\"", esp_wifi.ssid);
          return ESP_OK;
        }
        if (bits & WIFI_FAIL_BIT)
          break;
        ESP_LOGI(TAG, "Esperando asociacion/DHCP: %u/%u ms",
                 (unsigned)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS),
                 timeout_ms);
      }
      attempt_active = false;
      if (last_reason)
        ESP_LOGW(TAG, "Intento fallido reason=%u: %s", last_reason,
                 reason_name(last_reason));
      else
        ESP_LOGW(TAG, "Timeout Wi-Fi/DHCP (%u ms); no se obtuvo IP",
                 timeout_ms);
    }
    CHECK(stop_driver());
    if (attempt < attempts) {
      ESP_LOGW(TAG, "Siguiente intento Wi-Fi en 2 segundos");
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
  ESP_LOGE(TAG, "Agotados %u intentos a \"%s\"", attempts, esp_wifi.ssid);
  return ESP_FAIL;
}

void wifi_init_sta(void) {
  (void)wifi_connect_current(WIFI_ATTEMPTS, WIFI_ATTEMPT_TIMEOUT_MS);
}
void wifi_reconnect(void) {
  (void)wifi_connect_current(WIFI_ATTEMPTS, WIFI_ATTEMPT_TIMEOUT_MS);
}
