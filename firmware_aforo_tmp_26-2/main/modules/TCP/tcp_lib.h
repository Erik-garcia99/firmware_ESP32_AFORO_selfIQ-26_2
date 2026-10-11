#ifndef TCP_LIB_H
#define TCP_LIB_H
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "credential_parser.h"

#define RPI_IP_SETUP "192.168.50.1"
#define RPI_PORT_SETUP 5000
#ifndef TCP_CONNECT_TIMEOUT_MS
#define TCP_CONNECT_TIMEOUT_MS 8000
#endif
#ifndef TCP_IO_TIMEOUT_MS
#define TCP_IO_TIMEOUT_MS 15000
#endif
#ifndef TCP_MAX_ATTEMPTS
#define TCP_MAX_ATTEMPTS 5
#endif
#define TCP_DISCONNECTED BIT8
#define RETRY_SERVER BIT7
#define UPDATE_TCP BIT9
#define NO_RETRY_TCP BIT11

typedef struct {
    char *host_ip;
    uint16_t host_port;
    int sock;
    int connected;
    int logged_in;
} TYPE_TCP_CLIENT_T;
extern TYPE_TCP_CLIENT_T tcp_client;
extern EventGroupHandle_t g_tcp_event_group;

esp_err_t update_tcp_config(const char *ip, uint16_t port);
esp_err_t tcp_cliente_init(void);
esp_err_t send_message(void);
esp_err_t tcp_request_wifi(selfiq_credentials_t *credentials);
void selfiq_tcp_close(void);
/* Provisioning is now sequential. No recv_task/tcp_process_task are launched. */
#endif
