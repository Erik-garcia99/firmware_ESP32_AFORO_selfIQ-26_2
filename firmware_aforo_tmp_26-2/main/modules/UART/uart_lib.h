#ifndef UART_LIB_H
#define UART_LIB_H

// librerias
#include <driver/uart.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
// macros
#define BUFFER 1024

#define TX_PIN 19
#define RX_PIN 18

// varibales flobales

// colas freertos
//  extern QueueHandle_t uart_event;
typedef struct {
  uart_port_t port;
  QueueHandle_t event_queue;
} uart_task_config_t;

/**
 * @brief inicar un UART
 *
 * @param uart_sel selecciona puerto de uart a usar < UART0,UART1,UART2>
 * @param BaudRate establece al baudrate de comunicacion
 * @param uart_data_length establece el tamanio de la plabra que aceptara UART <
 * 5 , 6, 7 ,8 bits >
 * @param uart_parity establece la paridad del frame enviado o recibido < indica
 * si hubo perdida de datos en la comunicacion >
 * @param uart_stop_bit estableces el bits de parada del frame
 * @param RX pin de recepcion
 * @param TX pin de trasmision
 *
 * @param ret_queue regresa la cola en donde se estaran recibiendo los eventos
 * del uart, como cuando se recibe un byte, o cuando se llena el buffer, etc.
 * @return ESP_OK si se asigno correctamente el puerto de uart
 * @return ESP_FAIL si en algun punto fallo
 */
esp_err_t uart_init(uart_port_t uart_sel, int BoudRate,
                    uart_word_length_t uart_data_length,
                    uart_parity_t uart_parity, uart_stop_bits_t uart_stop_bits,
                    int TX, int RX, QueueHandle_t *ret_queue);

/**
 * @brief tarea, demonio que estara esuchando que se haya ingresado algo por
 * teclado, o por otros UART
 *
 * algo opcional
 *
 * @return cola, regresa una cola indirectamente, con la infromacio  ingresada
 * al uart hacia el sitio en donde lo necesitara
 *
 *
 */

void task_uart(void *params);

#endif
