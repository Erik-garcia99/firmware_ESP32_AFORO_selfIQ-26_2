/* *********************************************
 *              selfIQ 2026-2
 *
 * @autor erik garcia chavez
 * @date octuber 2026
 * @assginature : proyecto de carrera
 * @teacher : Jose Isabel Garcia Rocha
 * ************************************************* */

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <linux/limits.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "esp_log.h"

#include <esp_err.h>
#include <tcp_lib.h>

#include "global.h"
#include "tcp_lib.h"

const char *TAG = "TCP_CLIENT: ";

esp_err_t tcp_cliente_init() {

  // reinicamos todo antes de inciar
  tcp_client.connected = 0;
  if (tcp_client.sock >= 0) {
    close(tcp_client.sock);
    tcp_client.sock = -1;
  }

  // se realizan 5 intenteos antes de estabelcer fallo al intentear realizar la
  // conexion
  for (int n_retry = 0; n_retry < 5; n_retry++) {

    ESP_LOGI(TAG, "intento %d de establecer conexion", n_retry);

    // creamos el descriptor del socket
    tcp_client.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_client.sock < 0) {
      ESP_LOGE(TAG, "error al crear el descriptor del socket(): %d", errno);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue; // saltamos todo y se vuelve a intentar.
    }
    // timeout de recv
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(tcp_client.sock, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));

    // direccion del servidor
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(tcp_client.host_port),
    };

    inet_pton(AF_INET, tcp_client.host_ip, &server_addr.sin_addr);

    // conectar
    if (connect(tcp_client.sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
      ESP_LOGE(TAG, "descriptor <connect()> fallo  %d", errno);
      close(tcp_client.sock);
      tcp_client.sock = -1;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // exito, salir de inmediato
    tcp_client.connected = 1;
    ESP_LOGI(TAG, "\r\nconectado a %s:%d\r\n", tcp_client.host_ip,
             tcp_client.host_port);

    /*
    char conn_msg[64];
    snprintf(conn_msg, sizeof(conn_msg), "\r\nconectado a %s:%d\r\n",
             tcp_client.host_ip, tcp_client.host_port);
    uart_write_bytes(global_uart.NUM_PORT, UART_GREEN, strlen(UART_GREEN));
    uart_write_bytes(global_uart.NUM_PORT, conn_msg, strlen(conn_msg));
    uart_write_bytes(global_uart.NUM_PORT, UART_RESET, strlen(UART_RESET));
    */

    return ESP_OK;
  }

  // se agotaron los 5 intentos
  /*
   *  NOTA:
   *  en estos puntos por ejemplo, una vez que se monte en el enotnro real, no
   * habra manera de ver esta pantalla no tan facil, por lo que <archivo de
   * apuntos, expligo un poco Debug de ESP32>
   *
   *
   * */
  ESP_LOGI(TAG, "\r\nno se pudo conectar con el servidor : %s:%d",
           tcp_client.host_ip, tcp_client.host_port);

  /*
  char conn_msg[64];
  snprintf(conn_msg, sizeof(conn_msg),
           "\r\nno se pudo hacer la conexion con el servidor:%s:%d\r\n",
           mariposa!+65HusP!  uart_write_bytes(global_uart, NUM_PORT, UART_RED,
  strlen(UART_RED)); uart_write_bytes(global_uart.NUM_PORT, conn_msg,
  strlen(conn_msg)); uart_write_bytes(global_uart.NUM_PORT, UART_RED,
  strlen(UART_RED));
  */

  tcp_client.connected = 0;
  return ESP_FAIL;
}

// debemos de limpiar y actualizar con las estrucutra nueva de nuestro frame
// para la comnicacion con la RPI

void recv_task(void *params) {
  uint8_t rx_buffer[MAX_DATA];
  uint8_t *buffer_tmp;

  while (1) {
    char hex_buf[128] = {0};
    int pos = 0;

    int len = recv(tcp_client.sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      ESP_LOGE(TAG, "\r\nTCP_LIB: recv fallo errno: %d\r\n",  errno));
      tcp_client.connected = 0;
      tcp_client.logged_in = 0;
      if (tcp_client.sock >= 0) {
        close(tcp_client.sock);
        tcp_client.sock = -1;
      }
      xEventGroupSetBits(g_tcp_event_group, TCP_DISCONNECTED);
      vTaskDelete(NULL); // esta tarea ya no tiene socket que leer
    }

    else if (len == 0) {
      // conexion cerrada por el servidor

      ESP_LOGI(TAG, "\r\nTCP_LIB: el servidor cerro la conexion\r\n");
      tcp_client.connected = 0;
      tcp_client.logged_in = 0;
      close(tcp_client.sock);
      tcp_client.sock = -1;
      xEventGroupSetBits(g_tcp_event_group, TCP_DISCONNECTED);
      vTaskDelete(NULL); // esta tarea ya no tiene socket que leer
    }

    //+++++++++++++++++++++++++++imprimir lo que recibimos :  DEGUB - sera
    // eliminado solo srive el etapa de desarrollo

    for (int i = 0; i < len; i++) {
      pos +=
          snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02X ", rx_buffer[i]);
    }

    ESP_LOGI(TAG, "REV RAW: %s\n", hex_buf);

    uint8_t offset = 0;
    TYPE_FORMAT_REQUEST_T *frame = pvPortMalloc(sizeof(TYPE_FORMAT_REQUEST_T));

    if (frame == NULL) {

      ESP_LOGE(TAG, "\r\nsin memoria\r\n");
      continue;
    }

    uint16_t aux;
    memcpy(&frame->header, &rx_buffer[offset], 2);
    offset += 2;
    aux = frame->header;
    frame->header = ntohs(aux);

    // @update necesitamos modificar establecer otras caberas
    if (frame->header == HEADER) {

      // el len creo que no superara los 255 datos en binario,
      memcpy(&frame->len, &rx_buffer[offset], 1);
      offset++;

      uint8_t action_res = rx_buffer[offset++];

      frame->type_frame = (action_res >> 4) & 0x0f;
      frame->action = (action_res) & 0x0f; // de aqui sacaremos por ejemplo con
                                           // WIFI si es de empresa o no la red

      // este es un punto importante porque de este punto indicaremos si lo que
      // viene
      uint8_t value_len =
          (frame->len > 3) ? (frame->len - 3)
                           : 0; // del frame quitamos header y len para
                                // quedarnos con los datos improtates del frame
      if (value_len > MAX_DATA)
        value_len = MAX_DATA;
      if (value_len > 0) {
        memcpy(frame->data, &rx_buffer[offset], value_len);
      }
    }
    if (frame->header == ACK && rx_buffer[offset] == 0xff) {
      // llego un nack
      // la rasberry no comprendio lo que recibio
      memcpy(&frame->len, &rx_buffer[offset], 1);
      offset++;

      // aunque es contenido realmente basura, pues es para un mayor
      // aseguramiento sobre lo que llego, type_frame y action juntos seran
      // 0xFF y data 0, indicando el error del NACK
      uint8_t action_res = rx_buffer[offset++];

      frame->type_frame = (action_res >> 4) & 0x0f;
      frame->action = (action_res) & 0x0f;

      frame->data[offset] = 0;
    }

    if (frame->header == ACK && rx_buffer[offset] != 0xff) {
      // se recibio un ACK
      memcpy(&frame->len, &rx_buffer[offset], 1);
      offset++;

      uint8_t type_frame = rx_buffer[offset++];
      frame->type_frame =
          type_frame & 0x0f; // quiero apagar la parte MSB del byte, y
                             // quedarme solo con la parte baja
      uint8_t value_len =
          (frame->len > 3) ? (frame->len - 3)
                           : 0; // del frame quitamos header y len para
                                // quedarnos con los datos improtates del frame
      // copiamos la patte de la infromacion donde vienen las credenicales.
      if (value_len > MAX_DATA)
        value_len = MAX_DATA;
      if (value_len > 0) {
        memcpy(frame->data, &rx_buffer[offset], value_len);
      }
    }
    // encolar
    if (xQueueSend(tcp_rx_queue, &frame, 0) != pdTRUE) {
      vPortFree(frame);
    }
  }
}

/**
 *
 * funcion encargada de enviar datos hacia la RPI con la que se va a solicitar
 * recursos o algun datos que esta en ella.
 *
 * @UPDATE necesitamos modificar para la nueva estrucutra que necesitamos para
 * solcitar recursos por medio de SOCKET
 *
 */
esp_err_t send_message() {

  // lo maximo que puede enviar son 40 bytes estos puede variar
  uint8_t buffer[160]; // aun esoty decidenido cual es tamanio optimo, por
                       // mientras lo dejaremos asi.
  int offset = 0;
  int len;

  //@DEBUG
  char hex_buf[128] = {0};
  int pos = 0;

  switch (send_info.op_type) {

  case OPReqCredWifi: {

    uint16_t id = htons(HEADER);
    memcpy(buffer + offset, &id, 2);
    offset += 2;
    // len of frame
    // son 2 bytes porque ACTION + TYPE_FRAME = 1 byte
    // DATA 1 byte aunque es cero pero es un espacio dentro del frame
    // especialmente para ese dato
    // en len no cuenta ni el < HEADER > ni el pripio < len> .
    buffer[offset] = 2;
    offset++;

    // TYPE_FRAME + ACTION - son uno mismo con 1 nibble son 16 posibilidades
    // de cada uno,

    uint8_t req_cred = (eNoType << 4) | eReqWifi;
    buffer[offset++] = req_cred;

    // memset(buffer + offset, 0, MAX_DATA); // ponemos la parte de data en 0.
    buffer[offset++] = 0;
  } break;
  case OP_ACK: {

    /*
     * el ACK le va a indicar a la rasberry que este ESP32, ya recibio las
     * credenicales pero aun no podemos saber si son las correctas, porque
     * primero necesitamos descoenctarnos de la AP actual para inciar en el
     * WIFI normal
     *
     * */

    uint16_t id = htons(ACK);
    memcpy(buffer + offset, &id, 2);
    offset += 2;

    buffer[offset++] = 2; // 2 bytes de transferencia
    buffer[offset++] = (eRespACK << 4) | action_none; // 0003 0000
    buffer[offset++] = 1; // solo para diferenicar del NACK

  } break;
  case OP_NACK: {

    uint16_t id = htons(ACK);
    memcpy(buffer + offset, &id, 2);
    offset += 2;
    buffer[offset] = 0xFF;
    offset++;
    buffer[offset++] = 0xFF; // accion y tipo en maximo indciando error.
    buffer[offset++] = 0;
  } break;

  default: {
    ESP_LOGI(TAG, "OPCION INCORRECTA: ");
  } break;
  }
  int sent = send(tcp_client.sock, buffer, offset, 0);
  if (sent < 0) {
    char err_str[32];
    ESP_LOGE(TAG, "\r\nsend() fallo errno: %d\r\n", errno);

    return ESP_FAIL;
  }
  for (int i = 0; i < offset; i++) {
    pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02X ", buffer[i]);
  }

  ESP_LOGI(TAG, "DEBUG - FRAME SEND: %s", hex_buf);
  return ESP_OK;
}
