
#include "uart_lib.h"
#include "global.h"
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <esp_err.h>
#include <esp_log.h>
#include <string.h>

esp_err_t uart_init(uart_port_t uart_sel, int BaudRate,
                    uart_word_length_t uart_data_length,
                    uart_parity_t uart_parity, uart_stop_bits_t uart_stop_bits,
                    int TX, int RX, QueueHandle_t *ret_queue) {
  if (uart_is_driver_installed(uart_sel)) {
    uart_driver_delete(uart_sel);
  }

  uart_config_t cfg = {
      .baud_rate = BaudRate,
      .data_bits = uart_data_length,
      .parity = uart_parity,
      .stop_bits = uart_stop_bits,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(uart_sel, &cfg));
  ESP_ERROR_CHECK(
      uart_set_pin(uart_sel, TX, RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  QueueHandle_t queue = xQueueCreate(20, sizeof(uart_event_t));
  *ret_queue = queue; // FIX: dereference pointer

  ESP_ERROR_CHECK(
      uart_driver_install(uart_sel, BUFFER * 2, BUFFER * 2, 20, &queue, 0));
  uart_vfs_dev_use_driver(uart_sel);

  uart_write_bytes(uart_sel, "UART inicializado correctamente\n", 32);
  return ESP_OK;
}

void task_uart(void *params) {
  uart_task_config_t *cfg = (uart_task_config_t *)params;
  uart_port_t port = cfg->port;
  QueueHandle_t event_queue = cfg->event_queue;

  uart_event_t event;
  uint8_t *buffer = (uint8_t *)malloc(BUFFER);
  uint8_t input[BUFFER];
  int index_input = 0;

  while (1) {
    if (xQueueReceive(event_queue, &event, portMAX_DELAY)) {
      switch (event.type) {
      case UART_DATA: {
        int len = uart_read_bytes(port, buffer, event.size, portMAX_DELAY);
        for (int i = 0; i < len; i++) {
          char c = buffer[i];
          if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= 32 && c <= 63) || (c == '_')) {
            uart_write_bytes(port, (const char *)&c, 1);
            if (index_input < BUFFER - 1)
              input[index_input++] = c;
          } else if (c == '\n' || c == '\r') {
            input[index_input] = '\0';
            if (index_input > 0) {
              char *data_flow = (char *)malloc(strlen((char *)input) + 1);
              strcpy(data_flow, (char *)input);
              xQueueSend(flow_data_queue, &data_flow, portMAX_DELAY);
            }
            index_input = 0;
            memset(input, 0, BUFFER);
          } else if (c == '\b') {
            if (index_input > 0) {
              index_input--;
              uart_write_bytes(port, "\b \b", 3);
            }
          }
        }
      } break;

      case UART_BUFFER_FULL:
      case UART_FIFO_OVF:
        xQueueReset(event_queue);
        uart_flush(port);
        break;

      default: {
        char default_err[64];
        int len = snprintf(default_err, sizeof(default_err),
                           "ERROR: tipo de error -> %d\n", event.type);
        uart_write_bytes(port, default_err, len);
      } break;
      }
    }
  }
}
