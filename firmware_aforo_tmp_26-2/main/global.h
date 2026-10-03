#ifndef GLOBAL_H
#define GLOBAL_H

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define TRUE 1
#define FALSE 0

// ─── Bits del grupo de eventos WiFi ─────────────────────
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_UPDATE BIT10
#define BREAK_UPDATE_WIFI BIT11
#define WIFI_CREDS_READY BIT12
#define BROKER_RECEIVED BIT3 // usado para ACK de SET_WIFI

// ─── Nuevos bits ────────────────────────────────────────
#define WIFI_CREDS_RECEIVED BIT13 // El servidor HTTP capturó SSID/PSWD
// BROKER_IP_RECEIVED eliminado

// ─── Constantes generales ───────────────────────────────

// ─── Estructuras ────────────────────────────────────────
typedef struct {
  const char *alias;
  const char *ssid;
  const char *pswd;
} wifi_default_profile_t;

typedef struct {
  const char *alias;
  const char *ssid;
  const char *user;
  const char *pswd;
} wifi_default_ent_profile_t;

extern char g_wifi_ssid[33];
extern char g_wifi_pass[65];
extern char g_broker_ip[16];

extern esp_err_t nvs_save_str(const char *key, const char *value);
extern esp_err_t nvs_load_str(const char *key, char *buf, size_t len);

#endif
