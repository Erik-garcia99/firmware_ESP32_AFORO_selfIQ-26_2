#ifndef WIFI_LIB_H
#define WIFI_LIB_H
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT2
#define WIFI_UPDATE BIT3
#define BREAK_UPDATE_WIFI BIT4
#define WIFI_CREDS_READY BIT5
#define WIFI_CREDS_RECEIVED BIT6
#define WIFI_ATTEMPTS 5
#define WIFI_ATTEMPT_TIMEOUT_MS 30000
#define SELFIQ_SETUP_SSID "SelfIQ-Admi"
#define SELFIQ_SETUP_PASSWORD "$elfIQ-4dmi-1"

extern EventGroupHandle_t s_wifi_event_group;
typedef struct {
    char *ssid;
    char *pswd;
    char *user_name;
    esp_ip4_addr_t *ip;
    int type_connected; /* 0 Personal; 1 Enterprise */
    volatile int connected;
} TYPE_ESP_WIFI_T;
extern TYPE_ESP_WIFI_T esp_wifi;

esp_err_t wifi_connect_current(unsigned attempts, unsigned timeout_ms);
void wifi_init_sta(void); /* compatibility wrapper, bounded */
void wifi_reconnect(void); /* compatibility wrapper, bounded */
#endif
