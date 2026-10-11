#include "tcp_lib.h"
#include "cc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "global.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "modules/WIFI/wifi_lib.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "SELFIQ_TCP";

static const char *socket_hint(int e) {
  switch (e) {
  case ETIMEDOUT:
    return "Sin respuesta a tiempo: revise servidor/AP/firewall";
  case ECONNREFUSED:
    return "IP accesible pero puerto rechazado: revise selfiq:5000";
  case ENETUNREACH:
    return "Sin ruta: revise Wi-Fi e IP de la ESP";
  case EHOSTUNREACH:
    return "Host no accesible: revise IP y AP";
  case ECONNRESET:
    return "Servidor reinicio o cerro la conexion";
  default:
    return "Revise servidor, interfaz AP y conectividad";
  }
}
static void socket_error(const char *step, int e) {
  ESP_LOGE(TAG, "%s: errno=%d (%s). %s", step, e, strerror(e), socket_hint(e));
}

void selfiq_tcp_close(void) {
  if (tcp_client.sock >= 0) {
    ESP_LOGI(TAG, "Cerrando socket TCP fd=%d", tcp_client.sock);
    close(tcp_client.sock);
    tcp_client.sock = -1;
  }
  tcp_client.connected = 0;
  tcp_client.logged_in = 0;
}

esp_err_t update_tcp_config(const char *ip, uint16_t port) {
  ESP_LOGI(TAG, "Preparando destino TCP: %s:%u", ip ? ip : "(null)", port);
  struct in_addr parsed;
  if (!ip || !port || inet_pton(AF_INET, ip, &parsed) != 1) {
    ESP_LOGE(TAG, "Direccion IP o puerto invalido");
    return ESP_ERR_INVALID_ARG;
  }
  char *copy = malloc(strlen(ip) + 1);
  if (!copy) {
    ESP_LOGE(TAG, "Sin memoria para destino TCP");
    return ESP_ERR_NO_MEM;
  }
  strcpy(copy, ip);
  selfiq_tcp_close();
  free(tcp_client.host_ip);
  tcp_client.host_ip = copy;
  tcp_client.host_port = port;
  ESP_LOGI(TAG, "Destino TCP listo");
  return ESP_OK;
}

/* Bounded waits with progress messages. No blocking connect or recv. */
static int wait_socket(int fd, bool writing, int64_t deadline) {
  while (1) {
    if (!esp_wifi.connected) {
      errno = ENETUNREACH;
      return -1;
    }
    int64_t remaining = deadline - esp_timer_get_time();
    if (remaining <= 0) {
      errno = ETIMEDOUT;
      return 0;
    }
    fd_set fds, errors;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    FD_ZERO(&errors);
    FD_SET(fd, &errors);
    int64_t slice = remaining < 1000000 ? remaining : 1000000;
    struct timeval tv = {.tv_sec = slice / 1000000, .tv_usec = slice % 1000000};
    int n = select(fd + 1, writing ? NULL : &fds, writing ? &fds : NULL,
                   &errors, &tv);
    if (n > 0) {
      if (FD_ISSET(fd, &errors)) {
        int e = 0;
        socklen_t size = sizeof(e);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &size) < 0)
          return -1;
        errno = e ? e : ECONNRESET;
        return -1;
      }
      return 1;
    }
    if (n < 0 && errno != EINTR)
      return -1;
    ESP_LOGI(TAG, "Esperando %s; quedan %lld s",
             writing ? "conexion/envio" : "respuesta RPI",
             (long long)((deadline - esp_timer_get_time() + 999999) / 1000000));
  }
}

esp_err_t tcp_cliente_init(void) {
  selfiq_tcp_close();
  if (!tcp_client.host_ip || !tcp_client.host_port)
    return ESP_ERR_INVALID_STATE;
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_port = htons(tcp_client.host_port)};
  if (inet_pton(AF_INET, tcp_client.host_ip, &address.sin_addr) != 1)
    return ESP_ERR_INVALID_ARG;
  for (unsigned attempt = 1; attempt <= TCP_MAX_ATTEMPTS; ++attempt) {
    if (!esp_wifi.connected) {
      ESP_LOGE(TAG, "Sin Wi-Fi/IP; no se intenta TCP");
      return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Intento TCP %u/%u -> %s:%u (limite %u ms)", attempt,
             TCP_MAX_ATTEMPTS, tcp_client.host_ip, tcp_client.host_port,
             TCP_CONNECT_TIMEOUT_MS);
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
      socket_error("socket()", errno);
      goto retry;
    }
    tcp_client.sock = fd;
    ESP_LOGI(TAG, "Socket creado fd=%d; iniciando connect()", fd);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      socket_error("fcntl(O_NONBLOCK)", errno);
      goto retry;
    }
    int rc = connect(fd, (struct sockaddr *)&address, sizeof(address));
    if (rc < 0) {
      if (errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EALREADY) {
        socket_error("connect()", errno);
        goto retry;
      }
      int64_t deadline = esp_timer_get_time() + TCP_CONNECT_TIMEOUT_MS * 1000LL;
      if (wait_socket(fd, true, deadline) <= 0) {
        socket_error("connect()/select()", errno);
        goto retry;
      }
      int error = 0;
      socklen_t size = sizeof(error);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) {
        socket_error("getsockopt(SO_ERROR)", errno);
        goto retry;
      }
      if (error) {
        socket_error("Conexion TCP", error);
        goto retry;
      }
    }
    tcp_client.connected = 1;
    ESP_LOGI(TAG, "TCP conectado a %s:%u", tcp_client.host_ip,
             tcp_client.host_port);
    return ESP_OK;
  retry:
    selfiq_tcp_close();
    if (attempt < TCP_MAX_ATTEMPTS) {
      ESP_LOGW(TAG, "Reintento TCP en 2 segundos");
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
  ESP_LOGE(TAG, "Agotados %u intentos TCP; se volvera a comprobar el AP",
           TCP_MAX_ATTEMPTS);
  return ESP_FAIL;
}

static esp_err_t transfer(uint8_t *buffer, size_t count, bool writing,
                          int64_t deadline) {
    if (buffer == NULL && count > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t done = 0;

    while (done < count) {
        if (wait_socket(tcp_client.sock, writing, deadline) <= 0) {
            int error = errno;

            socket_error(writing ? "Timeout/error al enviar"
                                 : "Timeout/error al recibir",
                         error);

            return error == ETIMEDOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }

        ssize_t n = writing
                        ? send(tcp_client.sock, buffer + done,
                               count - done, 0)
                        : recv(tcp_client.sock, buffer + done,
                               count - done, 0);

        if (n > 0) {
            done += (size_t)n;

            ESP_LOGI(TAG, "%s %u/%u bytes",
                     writing ? "Enviados" : "Recibidos",
                     (unsigned)done, (unsigned)count);
        } else if (n < 0 &&
                   (errno == EINTR || errno == EAGAIN ||
                    errno == EWOULDBLOCK)) {
            continue;
        } else {
            if (n == 0) {
                ESP_LOGW(TAG,
                         "Transferencia interrumpida (%u/%u bytes)",
                         (unsigned)done, (unsigned)count);
            } else {
                socket_error(writing ? "send()" : "recv()", errno);
            }

            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t send_message(void) {
  if (tcp_client.sock < 0 || !tcp_client.connected) {

    return ESP_ERR_INVALID_STATE;
  }
  uint8_t bytes[5] = {0};

  switch (send_info.op_type) {

    case OPReqCredWifi: {
      uint16_t header = htons(HEADER);
      memcpy(bytes, &header, sizeof(HEADER));
      bytes[2] = 2;
      bytes[3] = eNoType << 4 | eReqWifi;
      bytes[4] = 0; // solo estamos estamos indicando que queremos solciitar las
                    // credenciales WIFI
      ESP_LOGI(TAG, "TX solicitud: AB CD 02 01 00");

    } break;
    
    case OPReqBroker{
      // ahora queremos realizar la peticion de la IP:PORT del broker para poder conectarnos 
    }

    case OP_ACK: {

      uint16_t header = htons(ACK);
      memcpy(bytes, &header, sizeof(ACK));
      bytes[2] = 2;
      bytes[3] = eRespACK << 4 | action_none;
      bytes[4] = 0; // solo estamos estamos indicando que queremos solciitar las
                    // credenciales WIFI
      ESP_LOGI(TAG, "TX ACK de recepcion (no confirma conexion Wi-Fi final)");

    } break;

    case OP_NACK: {
      uint16_t header = htons(ACK);
      memcpy(bytes, &header, sizeof(ACK));
      bytes[2] = 0xFF;
      bytes[3] = eRespNACK << 4 | action_none;
      bytes[4] = 0; // solo estamos estamos indicando que queremos solciitar las
                    // credenciales WIFI
      ESP_LOGI(TAG, "TX NACK de formato");
    }break;

    default:
    return ESP_ERR_INVALID_ARG; break;
  }

  return transfer(bytes, sizeof(bytes), true,
                  esp_timer_get_time() + TCP_IO_TIMEOUT_MS * 1000LL);
}

esp_err_t tcp_request_wifi(selfiq_credentials_t *credentials) {
  if (!credentials)
    return ESP_ERR_INVALID_ARG;
  send_info.op_type = OPReqCredWifi;
  esp_err_t err = send_message();
  if (err != ESP_OK)
    return err;
  ESP_LOGI(
      TAG,
      "Solicitud enviada; esperando cabecera y credenciales (limite %u ms)",
      TCP_IO_TIMEOUT_MS);
  uint8_t prefix[3], body[MAX_DATA + 1];
  int64_t deadline = esp_timer_get_time() + TCP_IO_TIMEOUT_MS * 1000LL;
  err = transfer(prefix, 3, false, deadline);
  if (err != ESP_OK)
    return err;
  uint16_t header = (uint16_t)prefix[0] << 8 | prefix[1];
  unsigned length = prefix[2];
  ESP_LOGI(TAG, "RX cabecera=0x%04X LEN=%u", header, length);
  if (header == ACK && length == 255) {
    err = transfer(body, 2, false, deadline);
    if (err != ESP_OK)
      return err;
    if (body[0] != 255 || body[1] != 0) {
      ESP_LOGE(TAG, "NACK mal formado");
      return ESP_FAIL;
    }
    ESP_LOGW(TAG, "RPI respondio NACK: aun no puede entregar credenciales. "
                  "Compruebe que RPI conecto a la red final");
    return ESP_ERR_INVALID_STATE;
  }
  if (header != ACK || length < 1 || length > MAX_DATA + 1) {
    ESP_LOGE(TAG, "Cabecera o LEN fuera de rango; no se copian datos");
    return ESP_FAIL;
  }
  err = transfer(body, length, false, deadline);
  if (err != ESP_OK)
    return err;
  const char *why = NULL;
  if (!selfiq_decode_credentials(header, length, body, credentials, &why)) {
    ESP_LOGE(TAG, "Respuesta rechazada: %s", why);
    send_info.op_type = OP_NACK;
    (void)send_message();
    return ESP_FAIL;
  }
  ESP_LOGI(TAG,
           "Credenciales TLV validas: tipo=%u SSID=\"%s\" (sin mostrar "
           "password/usuario)",
           credentials->kind, credentials->ssid);
  send_info.op_type = OP_ACK;
  err = send_message();
  if (err != ESP_OK)
    ESP_LOGW(
        TAG,
        "No pudo enviarse ACK, pero las credenciales recibidas son validas");
  return ESP_OK;
}
