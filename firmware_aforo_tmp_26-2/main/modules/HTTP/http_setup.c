#include "http_setup.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>

static httpd_handle_t server = NULL;
static EventGroupHandle_t event_group = NULL;

// Variables externas donde se guardan las credenciales
extern char g_wifi_ssid[33];
extern char g_wifi_pass[65];
extern esp_err_t nvs_save_str(const char *key, const char *value);

static const char *HTML_FORM =
    "<!DOCTYPE html><html>"
    "<head><meta charset='UTF-8'><title>Configurar WiFi</title></head>"
    "<body><h1>Configurar red WiFi</h1>"
    "<form method='POST' action='/save'>"
    "SSID: <input type='text' name='ssid'><br>"
    "Contraseña: <input type='password' name='pass'><br>"
    "<input type='submit' value='Guardar'>"
    "</form></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_send(req, HTML_FORM, strlen(HTML_FORM));

  return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
  char content[200] = {0};
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  content[ret] = '\0';

  // Extraer ssid=...&pass=...
  char ssid[33] = {0};
  char pass[65] = {0};
  char *ssid_ptr = strstr(content, "ssid=");
  char *pass_ptr = strstr(content, "pass=");
  if (ssid_ptr) {
    ssid_ptr += 5; // saltar "ssid="
    char *end = strstr(ssid_ptr, "&");
    if (end)
      *end = '\0';
    else
      end = ssid_ptr + strlen(ssid_ptr);
    strncpy(ssid, ssid_ptr, sizeof(ssid) - 1);
    // URL decode simple
    // (asumimos que no hay caracteres especiales)
  }
  if (pass_ptr) {
    pass_ptr += 5;
    char *end = strstr(pass_ptr, "&");
    if (end)
      *end = '\0';
    else
      end = pass_ptr + strlen(pass_ptr);
    strncpy(pass, pass_ptr, sizeof(pass) - 1);
  }

  if (strlen(ssid) == 0 || strlen(pass) == 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Guardar en las variables globales y en NVS
  strncpy(g_wifi_ssid, ssid, sizeof(g_wifi_ssid));
  strncpy(g_wifi_pass, pass, sizeof(g_wifi_pass));
  nvs_save_str("wifi_ssid", g_wifi_ssid);
  nvs_save_str("wifi_pass", g_wifi_pass);

  // Señalizar al main que ya tenemos credenciales
  xEventGroupSetBits(event_group, WIFI_CREDS_RECEIVED);

  // Enviar respuesta al navegador
  const char *resp = "<h1>Credenciales guardadas.</h1><p>El ESP se conectará a "
                     "la red WiFi.</p>";
  httpd_resp_send(req, resp, strlen(resp));
  vTaskDelay(
      pdMS_TO_TICKS(500)); // Asegura que el navegador reciba la respuesta
  esp_restart();
  return ESP_OK;
}

void start_http_setup_server(EventGroupHandle_t wifi_event_group) {
  event_group = wifi_event_group;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
  httpd_handle_t handle = NULL;
  if (httpd_start(&handle, &config) == ESP_OK) {
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(handle, &root);

    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
    };
    httpd_register_uri_handler(handle, &save);

    server = handle;
    ESP_LOGI("http_setup", "Servidor web iniciado");
  }
}

void stop_http_setup_server(void) {
  if (server) {
    httpd_stop(server);
    server = NULL;
  }
}
