#ifndef HTTP_SETUP_H
#define HTTP_SETUP_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

/*******************************************************************************************/
/*  note: */
/*                                                                                         */
/*    cunado se lanza por primera vez la esp32, sea la nano o la esp32CAM por */
/*    defecto */
/*    no tendra credenciales, por lo que primero se lanza como Acces point */
/*                                                                                         */
/*    por lo que tendra como credenciales: */
/*                                                                                         */
/*     SSID : SelfIQ-SETUP */
/*     psw: selfIQ-Admin1 */
/*                                                                                         */
/*    esp32sto sera accesibl desde la IP: 192.168.4.1 */
/*                                                                                         */
/*    la cual se ingresa en cualquier navegador y se podra ingresar las */
/*    credenciales */
/*    del WIFI del local, una vez hecho esto la esp32 se reinicara ahora con las
 */
/*                                                                                         */
/*    credeinciales nuevas, y no se volvera a pedir porque estan guardaras en */
/*    flash, pero se pueden actualizar sin probelma alguno desde la APP. */
/*                                                                                         */
/*******************************************************************************************/

/**
 * @brief inicia el servidor HTTP se lanza una app web sencilla para ingresar
 * las credeniclaes:
 *
 * */
void start_http_setup_server(EventGroupHandle_t wifi_event_group);
void stop_http_setup_server(void);

#endif
