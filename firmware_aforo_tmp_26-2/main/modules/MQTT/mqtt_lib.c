#include "mqtt_lib.h"
#include "identity_parser.h"
#include "modules/WIFI/wifi_lib.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "lwip/inet.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include "lwip/sockets.h"

#define STOP_BIT BIT3
#define STOPPED_BIT BIT4
#define NODE_ID_SIZE 37
static const char *TAG = "SELFIQ_MQTT";
EventGroupHandle_t g_mqtt_event_group;
static selfiq_broker_t broker;
static esp_mqtt_client_handle_t client;
static TaskHandle_t worker;
static SemaphoreHandle_t lock;
static QueueHandle_t id_queue;
static char node_id[NODE_ID_SIZE];
static char mac[18], reply_topic[64], client_id[40];
static atomic_int subscription_id = -1;
/* Fragmentos solo se manipulan desde el callback MQTT. */
static char incoming[NODE_ID_SIZE];
static size_t received;
static bool collecting;

static esp_err_t resources(void) {
  if (!g_mqtt_event_group) g_mqtt_event_group = xEventGroupCreate();
  if (!lock) lock = xSemaphoreCreateMutex();
  if (!id_queue) id_queue = xQueueCreate(1, NODE_ID_SIZE);
  return g_mqtt_event_group && lock && id_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t update_mqtt_config(const char *ip, uint16_t port) {
  struct in_addr address;
  if (!ip || strlen(ip) >= sizeof(broker.ip) || !port ||
      inet_pton(AF_INET, ip, &address) != 1)
    return ESP_ERR_INVALID_ARG;
  uint32_t host = ntohl(address.s_addr);
  if ((host >> 24) == 0 || (host >> 24) == 127 || (host >> 24) >= 224)
    return ESP_ERR_INVALID_ARG;
  if (client) return ESP_ERR_INVALID_STATE;
  /* ip puede apuntar a broker.ip: usar temporal antes de reemplazar. */
  selfiq_broker_t tmp = {.port = port};
  strcpy(tmp.ip, ip);
  broker = tmp;
  return ESP_OK;
}

esp_err_t mqtt_save_broker(void) {
  if (!broker.ip[0] || !broker.port) return ESP_ERR_INVALID_STATE;
  nvs_handle_t handle;
  esp_err_t err = nvs_open("selfiq_mqtt", NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_str(handle, "broker_ip", broker.ip);
  if (err == ESP_OK) err = nvs_set_u16(handle, "broker_port", broker.port);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t mqtt_load_broker(selfiq_broker_t *output) {
  if (!output) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  selfiq_broker_t tmp = {0};
  esp_err_t err = nvs_open("selfiq_mqtt", NVS_READONLY, &handle);
  if (err != ESP_OK) return err;
  size_t size = sizeof(tmp.ip);
  err = nvs_get_str(handle, "broker_ip", tmp.ip, &size);
  if (err == ESP_OK) err = nvs_get_u16(handle, "broker_port", &tmp.port);
  nvs_close(handle);
  if (err == ESP_OK) err = update_mqtt_config(tmp.ip, tmp.port);
  if (err == ESP_OK) *output = tmp;
  return err;
}

static esp_err_t save_node_id(const char *id) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("selfiq_mqtt", NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_str(handle, "node_id", id);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t mqtt_get_node_id(char *output, size_t capacity) {
  if (!output || capacity < NODE_ID_SIZE) return ESP_ERR_INVALID_ARG;
  if (!lock) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(lock, portMAX_DELAY);
  esp_err_t err = node_id[0] ? ESP_OK : ESP_ERR_INVALID_STATE;
  if (err == ESP_OK) memcpy(output, node_id, NODE_ID_SIZE);
  else output[0] = 0;
  xSemaphoreGive(lock);
  return err;
}

/* Desde la tarea de aplicacion, nunca desde callback MQTT.
 * enqueue evita bloquear sensores esperando una transferencia de red. */
esp_err_t mqtt_publish(const char *topic, const char *payload, int qos, bool retain) {
  if (!topic || !topic[0] || !payload || qos < 0 || qos > 2 ||
      strchr(topic, '#') || strchr(topic, '+')) return ESP_ERR_INVALID_ARG;
  if (!lock) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(lock, portMAX_DELAY);
  EventBits_t bits = xEventGroupGetBits(g_mqtt_event_group);
  int msg = -1;
  if (client && esp_wifi.connected && !(bits & STOP_BIT) &&
      (bits & (MQTT_CONNECTED_BIT | MQTT_REGISTERED_BIT)) ==
      (MQTT_CONNECTED_BIT | MQTT_REGISTERED_BIT))
    msg = esp_mqtt_client_enqueue(client, topic, payload, 0, qos, retain, true);
  xSemaphoreGive(lock);
  return msg >= 0 ? ESP_OK : ESP_FAIL;
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
  (void)arg; (void)base;
  esp_mqtt_event_handle_t event = data;
  switch (event_id) {
  case MQTT_EVENT_CONNECTED:
    collecting = false;
    xEventGroupClearBits(g_mqtt_event_group, MQTT_SUBSCRIBED_BIT);
    xEventGroupSetBits(g_mqtt_event_group, MQTT_CONNECTED_BIT);
    subscription_id = esp_mqtt_client_subscribe(event->client, reply_topic, 1);
    ESP_LOGI(TAG, "Broker conectado; suscribiendo %s", reply_topic);
    break;
  case MQTT_EVENT_SUBSCRIBED:
    /* No publicar la MAC hasta que el broker confirme la suscripcion. */
    if (event->msg_id == subscription_id) {
      xEventGroupSetBits(g_mqtt_event_group, MQTT_SUBSCRIBED_BIT);
      ESP_LOGI(TAG, "Suscripcion confirmada; listo para solicitar ID");
    }
    break;
  case MQTT_EVENT_DISCONNECTED:
    subscription_id = -1;
    collecting = false;
    xEventGroupClearBits(g_mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_SUBSCRIBED_BIT);
    ESP_LOGW(TAG, "Broker desconectado; ESP-MQTT reintentara");
    break;
  case MQTT_EVENT_DATA: {
    if (event->current_data_offset == 0) {
      collecting = event->topic && event->topic_len == (int)strlen(reply_topic) &&
                   !memcmp(event->topic, reply_topic, event->topic_len) &&
                   event->total_data_len == 36;
      received = 0;
    }
    if (!collecting) break;
    if (!event->data || event->data_len < 0 || event->current_data_offset < 0 ||
        event->total_data_len != 36 || (size_t)event->current_data_offset != received ||
        (size_t)event->data_len > 36 - received) {
      collecting = false;
      break;
    }
    memcpy(incoming + received, event->data, event->data_len);
    received += event->data_len;
    if (received == 36) {
      incoming[36] = 0;
      collecting = false;
      if (selfiq_valid_node_id(incoming, 36)) xQueueOverwrite(id_queue, incoming);
      else ESP_LOGW(TAG, "ID MQTT rechazado: se requiere UUID de 36 caracteres");
    }
    break;
  }
  case MQTT_EVENT_ERROR:
    ESP_LOGW(TAG, "Error MQTT; revisar listener Mosquitto, IP, puerto y ACL");
    break;
  default: break;
  }
}

void task_mqtt_heartbeat(void *arg) {
  (void)arg;
  TickType_t last_request = xTaskGetTickCount() - pdMS_TO_TICKS(10000);
  TickType_t last_heartbeat = xTaskGetTickCount() - pdMS_TO_TICKS(30000);
  bool was_ready = false;
  TickType_t last_subscribe = xTaskGetTickCount();
  while (!(xEventGroupGetBits(g_mqtt_event_group) & STOP_BIT)) {
    char candidate[NODE_ID_SIZE];
    if (xQueueReceive(id_queue, candidate, 0) == pdTRUE) {
      char current[NODE_ID_SIZE];
      bool have_id = mqtt_get_node_id(current, sizeof(current)) == ESP_OK;
      if (have_id && strcmp(current, candidate)) {
        ESP_LOGW(TAG, "ID distinto al persistido: se conserva identidad; revisar RPI");
      } else if (!have_id) {
        esp_err_t err = save_node_id(candidate);
        if (err == ESP_OK) {
          xSemaphoreTake(lock, portMAX_DELAY);
          memcpy(node_id, candidate, sizeof(node_id));
          xSemaphoreGive(lock);
          xEventGroupSetBits(g_mqtt_event_group, MQTT_REGISTERED_BIT);
          ESP_LOGI(TAG, "ID persistido: %s", candidate);
        } else ESP_LOGE(TAG, "No se guardo ID: %s; se repetira solicitud", esp_err_to_name(err));
      }
    }
    EventBits_t bits = xEventGroupGetBits(g_mqtt_event_group);
    /* Reintentar SUBSCRIBE si no hubo SUBACK, incluido outbox lleno. */
    TickType_t subscribe_now = xTaskGetTickCount();
    if (esp_wifi.connected && (bits & MQTT_CONNECTED_BIT) &&
        !(bits & MQTT_SUBSCRIBED_BIT) &&
        (TickType_t)(subscribe_now - last_subscribe) >= pdMS_TO_TICKS(10000)) {
      subscription_id = esp_mqtt_client_subscribe(client, reply_topic, 1);
      last_subscribe = subscribe_now;
    }
    bool ready = esp_wifi.connected && (bits & MQTT_CONNECTED_BIT) &&
                 (bits & MQTT_SUBSCRIBED_BIT);
    TickType_t now = xTaskGetTickCount();
    if (!ready) was_ready = false;
    if (ready && !(bits & MQTT_REGISTERED_BIT) &&
        (!was_ready || (TickType_t)(now - last_request) >= pdMS_TO_TICKS(10000))) {
      int msg = esp_mqtt_client_enqueue(client, "selfiq/broker", mac, 0, 1, false, true);
      ESP_LOGI(TAG, "Solicitud de ID por MAC %s: %s", mac, msg >= 0 ? "en cola" : "fallo");
      last_request = now;
    }
    if (ready && (bits & MQTT_REGISTERED_BIT) &&
        (!was_ready || (TickType_t)(now - last_heartbeat) >= pdMS_TO_TICKS(30000))) {
      char id[NODE_ID_SIZE], topic[96];
      if (mqtt_get_node_id(id, sizeof(id)) == ESP_OK) {
        snprintf(topic, sizeof(topic), "selfiq/node/%s/heartbeat", id);
        (void)mqtt_publish(topic, "alive", 1, false);
      }
      last_heartbeat = now;
    }
    was_ready = ready;
    /* El evento de parada despierta inmediatamente la tarea. */
    xEventGroupWaitBits(g_mqtt_event_group, STOP_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
  }
  xEventGroupSetBits(g_mqtt_event_group, STOPPED_BIT);
  vTaskDelete(NULL);
}

esp_err_t mqtt_stop(void) {
  if (!client) return ESP_OK;
  /* Fuera del callback MQTT; esperar que termine el productor antes de destruir. */
  if (worker) {
    xEventGroupSetBits(g_mqtt_event_group, STOP_BIT);
    xEventGroupWaitBits(g_mqtt_event_group, STOPPED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    worker = NULL;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  esp_err_t err = esp_mqtt_client_stop(client);
  if (err == ESP_OK) {
    err = esp_mqtt_client_destroy(client);
    if (err == ESP_OK) client = NULL;
  }
  xSemaphoreGive(lock);
  xEventGroupClearBits(g_mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_SUBSCRIBED_BIT);
  return err;
}

esp_err_t mqtt_init(EventGroupHandle_t wifi_events, const char *broker_ip) {
  /* wifi_events se conserva en la API existente. esp_wifi.connected es el
   * indicador disponible en el codigo entregado; no se inventa un bit WiFi. */
  if (!wifi_events || !esp_wifi.connected) return ESP_ERR_INVALID_STATE;
  if (client) return worker ? ESP_OK : ESP_ERR_INVALID_STATE;
  esp_err_t err = update_mqtt_config(broker_ip, broker.port ? broker.port : SELFIQ_MQTT_DEFAULT_PORT);
  if (err != ESP_OK) return err;
  err = resources();
  if (err != ESP_OK) return err;
  xEventGroupClearBits(g_mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_SUBSCRIBED_BIT |
                      MQTT_REGISTERED_BIT | STOP_BIT | STOPPED_BIT);
  xQueueReset(id_queue);
  node_id[0] = 0;
  nvs_handle_t handle;
  err = nvs_open("selfiq_mqtt", NVS_READONLY, &handle);
  if (err == ESP_OK) {
    size_t size = sizeof(node_id);
    err = nvs_get_str(handle, "node_id", node_id, &size);
    nvs_close(handle);
    node_id[sizeof(node_id) - 1] = 0;
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_INVALID_LENGTH)
      return err;
    if (err != ESP_OK || !selfiq_valid_node_id(node_id, strlen(node_id))) node_id[0] = 0;
  } else if (err != ESP_ERR_NVS_NOT_FOUND) {
    return err;
  }
  if (node_id[0]) xEventGroupSetBits(g_mqtt_event_group, MQTT_REGISTERED_BIT);
  uint8_t address[6];
  err = esp_read_mac(address, ESP_MAC_WIFI_STA);
  if (err != ESP_OK) return err;
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", address[0], address[1],
           address[2], address[3], address[4], address[5]);
  snprintf(reply_topic, sizeof(reply_topic), "selfiq/broker/get_id/%s", mac);
  snprintf(client_id, sizeof(client_id), "selfiq-%s", mac);
  esp_mqtt_client_config_t config = {
    .broker.address.hostname = broker.ip,
    .broker.address.port = broker.port,
    .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
    .credentials.client_id = client_id,
    .network.reconnect_timeout_ms = 5000,
  };
  client = esp_mqtt_client_init(&config);
  if (!client) return ESP_ERR_NO_MEM;
  err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
  if (err == ESP_OK) err = esp_mqtt_client_start(client);
  if (err != ESP_OK) {
    esp_mqtt_client_destroy(client);
    client = NULL;
    return err;
  }
  if (xTaskCreate(task_mqtt_heartbeat, "selfiq_mqtt", 4096, NULL, 5, &worker) != pdPASS) {
    (void)mqtt_stop();
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "MQTT iniciado hacia %s:%u; MAC=%s", broker.ip, broker.port, mac);
  return ESP_OK;
}
